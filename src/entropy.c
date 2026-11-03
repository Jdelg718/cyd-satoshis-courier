// Entropy pool.
//
// Design note on honesty: esp_random() is only a TRUE random generator while a
// physical entropy source is running. Espressif's documented pattern for an app
// that uses the ADC (we do) is to enable the bootloader entropy source briefly,
// seed a software DRBG from it, then disable it again. That DRBG is the real
// cryptographic floor. Gameplay timing, mic noise and floating-pin ADC jitter
// are folded in as *additional* reseed material - they are the game mechanic,
// not the security basis. See ESP-IDF "Random Number Generation".
#include <string.h>
#include "entropy.h"
#include "board.h"
#include "audio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "bootloader_random.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"

static const char *TAG = "entropy";

static mbedtls_ctr_drbg_context drbg;
static mbedtls_entropy_context  ent_ctx;
static mbedtls_sha256_context   pool;        // running hash of gameplay material

static uint32_t n_actions = 0;
static uint32_t play_bits = 0;
static int64_t  run_start_us = 0;
static int64_t  last_event_us = 0;
static bool     ready = false;
static char     src_desc[72] = "";

// Conservative per-event estimates. Human inter-action timing is correlated, so
// we claim far less than the raw microsecond delta would suggest.
#define BITS_PER_ACTION   4
#define BITS_PER_HW_BATCH 8
#define PLAY_BITS_CAP     512

static void pool_feed(const void *p, size_t n) { mbedtls_sha256_update(&pool, (const uint8_t *)p, n); }

bool entropy_init(void)
{
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_entropy_init(&ent_ctx);
    mbedtls_sha256_init(&pool);
    mbedtls_sha256_starts(&pool, 0);

    // --- true-random floor: enable the hardware entropy source, harvest, disable ---
    uint8_t seed[48];
    bootloader_random_enable();
    esp_fill_random(seed, sizeof(seed));
    bootloader_random_disable();     // MUST happen before any ADC use

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    uint8_t personal[64];
    memcpy(personal, seed, sizeof(seed));
    memcpy(personal + 48, mac, 6);
    int64_t t = esp_timer_get_time();
    memcpy(personal + 54, &t, 8);
    personal[62] = 0x53; personal[63] = 0x43;   // "SC"

    int rc = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &ent_ctx, personal, sizeof(personal));
    memset(seed, 0, sizeof(seed));
    memset(personal, 0, sizeof(personal));
    if (rc != 0) {
        ESP_LOGE(TAG, "ctr_drbg_seed failed: -0x%04x", -rc);
        return false;
    }
    mbedtls_ctr_drbg_set_prediction_resistance(&drbg, MBEDTLS_CTR_DRBG_PR_OFF);

    snprintf(src_desc, sizeof(src_desc), "HW-TRNG + CTR-DRBG + play + mic + ADC");
    ready = true;
    ESP_LOGI(TAG, "pool ready: %s", src_desc);
    return true;
}

void entropy_begin_run(void)
{
    mbedtls_sha256_free(&pool);
    mbedtls_sha256_init(&pool);
    mbedtls_sha256_starts(&pool, 0);
    n_actions = 0;
    play_bits = 0;
    run_start_us = esp_timer_get_time();
    last_event_us = run_start_us;
}

void entropy_event(ev_kind_t kind, int32_t a, int32_t b)
{
    int64_t now = esp_timer_get_time();
    int64_t delta = now - last_event_us;
    last_event_us = now;

    struct __attribute__((packed)) {
        uint8_t kind; int32_t a, b; int64_t now, delta; uint32_t rnd;
    } rec = { (uint8_t)kind, a, b, now, delta, esp_random() };
    pool_feed(&rec, sizeof(rec));

    n_actions++;
    if (play_bits < PLAY_BITS_CAP) play_bits += BITS_PER_ACTION;
}

void entropy_sample_hw(void)
{
    // Floating expansion-header pins: measured ~380 LSB of jitter per 64 reads.
    int16_t adc[16];
    for (int i = 0; i < 16; i++)
        adc[i] = (int16_t)board_adc_raw(i & 1 ? ADC_CHANNEL_2 : ADC_CHANNEL_1);
    pool_feed(adc, sizeof(adc));

    // MEMS microphone - genuine analog noise, and free since the codec is live.
    int16_t mic[64];
    size_t got = audio_read(mic, 64);
    if (got) pool_feed(mic, got * sizeof(int16_t));

    uint32_t r = esp_random();
    int64_t  t = esp_timer_get_time();
    pool_feed(&r, sizeof(r));
    pool_feed(&t, sizeof(t));

    if (play_bits < PLAY_BITS_CAP) play_bits += BITS_PER_HW_BATCH;
}

uint32_t entropy_actions(void)  { return n_actions; }
uint32_t entropy_play_bits(void){ return play_bits > PLAY_BITS_CAP ? PLAY_BITS_CAP : play_bits; }
uint32_t entropy_run_ms(void)   { return (uint32_t)((esp_timer_get_time() - run_start_us) / 1000); }
const char *entropy_sources(void) { return src_desc; }

bool entropy_generate(uint8_t *out, size_t n)
{
    if (!ready || !out) return false;

    // Fold the whole gameplay session into the DRBG as additional input, then draw.
    uint8_t digest[32];
    mbedtls_sha256_context snap;
    mbedtls_sha256_init(&snap);
    mbedtls_sha256_clone(&snap, &pool);
    mbedtls_sha256_finish(&snap, digest);
    mbedtls_sha256_free(&snap);

    int rc = mbedtls_ctr_drbg_reseed(&drbg, digest, sizeof(digest));
    memset(digest, 0, sizeof(digest));
    if (rc != 0) { ESP_LOGE(TAG, "reseed failed: -0x%04x", -rc); return false; }

    rc = mbedtls_ctr_drbg_random(&drbg, out, n);
    if (rc != 0) { ESP_LOGE(TAG, "drbg_random failed: -0x%04x", -rc); return false; }
    return true;
}
