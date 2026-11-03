// Procedural audio engine: every sound is synthesised at boot into PSRAM, so
// the firmware ships no audio assets. A mixer task on core 1 sums the active
// one-shot voices with a live chiptune sequencer and feeds the ES8311.
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "sfx.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "sfx";

#define SR          AUDIO_SAMPLE_RATE
#define MIX_BLOCK   256
#define MAX_VOICES  8

typedef struct { int16_t *data; int len; } clip_t;
typedef struct { const int16_t *data; int len; int pos; float vol; bool active; } voice_t;

static clip_t  clips[SFX_COUNT];
static voice_t voices[MAX_VOICES];
static SemaphoreHandle_t vlock;
static const float vol_table[SFX_LEVELS] = { 0.00f, 0.16f, 0.42f, 0.85f };
static const char *vol_names[SFX_LEVELS]  = { "MUTE", "LOW", "MED", "HIGH" };
static int   vol_level = 2;                      // default MED, not full blast
static float master = 0.42f;

// ---------------- tiny synth helpers ----------------
static uint32_t rng = 0x13579BDF;
static inline float noise(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (int32_t)rng / 2147483648.0f;
}
static inline float sqr(float ph, float duty) { return (ph - floorf(ph)) < duty ? 1.0f : -1.0f; }

static clip_t mk(int ms)
{
    clip_t c;
    c.len = SR * ms / 1000;
    c.data = heap_caps_calloc(c.len, sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!c.data) c.len = 0;
    return c;
}
static inline void put(clip_t *c, int i, float v)
{
    if (i < 0 || i >= c->len) return;
    if (v > 1.0f) v = 1.0f; if (v < -1.0f) v = -1.0f;
    int32_t s = c->data[i] + (int32_t)(v * 26000.0f);
    c->data[i] = s > 32767 ? 32767 : (s < -32768 ? -32768 : s);
}

// A blip: square tone sweeping f0->f1 with exponential decay.
static void blip(clip_t *c, int off, int ms, float f0, float f1, float amp, float duty)
{
    int n = SR * ms / 1000;
    float ph = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / n;
        float f = f0 + (f1 - f0) * t;
        float env = expf(-4.0f * t) * amp;
        if (i < 64) env *= i / 64.0f;                 // click-free attack
        put(c, off + i, sqr(ph, duty) * env);
        ph += f / SR;
    }
}

static void noiseburst(clip_t *c, int off, int ms, float amp, float decay, float lp)
{
    int n = SR * ms / 1000;
    float z = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / n;
        z += (noise() - z) * lp;                      // one-pole lowpass -> "body"
        put(c, off + i, z * expf(-decay * t) * amp);
    }
}

static void chord(clip_t *c, int off, int ms, const float *freqs, int nf, float amp)
{
    int n = SR * ms / 1000;
    float ph[8] = {0};
    for (int i = 0; i < n; i++) {
        float t = (float)i / n;
        float env = (t < 0.02f ? t / 0.02f : expf(-2.2f * (t - 0.02f))) * amp;
        float s = 0.0f;
        for (int k = 0; k < nf; k++) { s += sqr(ph[k], 0.5f); ph[k] += freqs[k] / SR; }
        put(c, off + i, s / nf * env);
    }
}

static void synthesise(void)
{
    // Coin: the classic two-step rising blip.
    clips[SFX_COIN] = mk(140);
    blip(&clips[SFX_COIN], 0,             50, 988.0f, 988.0f, 0.55f, 0.5f);
    blip(&clips[SFX_COIN], SR * 45 / 1000, 95, 1319.0f, 1319.0f, 0.55f, 0.5f);

    // Higher variant for combo pickups.
    clips[SFX_COIN_HI] = mk(140);
    blip(&clips[SFX_COIN_HI], 0,             45, 1319.0f, 1319.0f, 0.5f, 0.5f);
    blip(&clips[SFX_COIN_HI], SR * 40 / 1000, 95, 1976.0f, 1976.0f, 0.5f, 0.5f);

    // Crash: noise body + pitch-dropping tone underneath.
    clips[SFX_CRASH] = mk(420);
    noiseburst(&clips[SFX_CRASH], 0, 420, 0.75f, 4.0f, 0.22f);
    blip(&clips[SFX_CRASH], 0, 300, 420.0f, 60.0f, 0.5f, 0.35f);

    // Shield pickup: upward sweep.
    clips[SFX_SHIELD] = mk(260);
    blip(&clips[SFX_SHIELD], 0, 260, 330.0f, 1400.0f, 0.42f, 0.3f);

    // Level up: four-note arpeggio.
    clips[SFX_LEVELUP] = mk(420);
    const float arp[4] = {523.25f, 659.25f, 783.99f, 1046.50f};
    for (int i = 0; i < 4; i++)
        blip(&clips[SFX_LEVELUP], SR * (i * 70) / 1000, 150, arp[i], arp[i], 0.42f, 0.5f);

    // Game over: slow descending minor fall.
    clips[SFX_GAMEOVER] = mk(900);
    const float fall[4] = {440.0f, 392.0f, 329.63f, 261.63f};
    for (int i = 0; i < 4; i++)
        blip(&clips[SFX_GAMEOVER], SR * (i * 190) / 1000, 320, fall[i], fall[i] * 0.98f, 0.45f, 0.45f);

    clips[SFX_CLICK] = mk(40);
    blip(&clips[SFX_CLICK], 0, 32, 1500.0f, 900.0f, 0.3f, 0.5f);

    clips[SFX_DENY] = mk(220);
    blip(&clips[SFX_DENY], 0, 110, 220.0f, 180.0f, 0.4f, 0.5f);
    blip(&clips[SFX_DENY], SR * 110 / 1000, 110, 165.0f, 130.0f, 0.4f, 0.5f);

    // Seed generated: big shimmering major chord.
    clips[SFX_SEED] = mk(1300);
    const float maj[4] = {261.63f, 329.63f, 392.00f, 523.25f};
    chord(&clips[SFX_SEED], 0, 1300, maj, 4, 0.5f);
    const float maj2[3] = {523.25f, 659.25f, 783.99f};
    chord(&clips[SFX_SEED], SR * 260 / 1000, 1040, maj2, 3, 0.32f);

    clips[SFX_WHOOSH] = mk(300);
    noiseburst(&clips[SFX_WHOOSH], 0, 300, 0.35f, 2.2f, 0.06f);

    size_t total = 0;
    for (int i = 0; i < SFX_COUNT; i++) total += clips[i].len * 2;
    ESP_LOGI(TAG, "synthesised %d effects, %u KB in PSRAM", SFX_COUNT, (unsigned)(total / 1024));
}

// ---------------- chiptune sequencer ----------------
// 16th notes at 132 BPM. Three channels: square bass, square lead, noise perc.
static bool  music_on = false;
static int   intensity = 0;
static int   seq_step = 0, step_left = 0;
static float bph, lph, lph2;
static float bfreq, lfreq;
static float benv, lenv, penv;
static float pdecay = 40.0f;

#define STEP_SAMPLES (SR * 60 / 132 / 4)

static inline float midi(int n) { return 440.0f * powf(2.0f, (n - 69) / 12.0f); }

// A-minor drive. 0 = rest, otherwise a MIDI note number.
static const uint8_t bass_pat[16] = { 33,0,33,0, 31,0,31,0, 36,0,36,0, 28,0,28,33 };
static const uint8_t lead_pat[32] = {
    69,0,72,0, 76,0,72,0, 67,0,72,0, 76,0,79,0,
    69,0,72,0, 76,0,81,0, 79,0,76,0, 72,0,69,0
};
static const uint8_t perc_pat[16] = { 2,0,1,0, 3,0,1,0, 2,0,1,0, 3,1,1,1 };

static void seq_advance(void)
{
    int b = bass_pat[seq_step & 15];
    if (b) { bfreq = midi(b); benv = 1.0f; }

    if (intensity >= 1) {
        int l = lead_pat[seq_step & 31];
        if (l) { lfreq = midi(l + (intensity >= 3 ? 12 : 0)); lenv = 1.0f; }
    }
    if (intensity >= 2) {
        int p = perc_pat[seq_step & 15];
        if (p) { penv = (p == 3 ? 1.0f : (p == 2 ? 0.9f : 0.35f)); pdecay = (p == 1) ? 90.0f : 26.0f; }
    }
    seq_step++;
    step_left = STEP_SAMPLES;
}

static inline float music_sample(void)
{
    if (!music_on) return 0.0f;
    if (step_left-- <= 0) seq_advance();

    float s = 0.0f;
    // bass
    s += sqr(bph, 0.5f) * benv * 0.30f;
    bph += bfreq / SR; benv *= 0.99988f;
    // detuned twin-square lead
    if (intensity >= 1) {
        s += (sqr(lph, 0.5f) + sqr(lph2, 0.52f)) * 0.5f * lenv * 0.20f;
        lph += lfreq / SR; lph2 += lfreq * 1.006f / SR; lenv *= 0.99975f;
    }
    // noise percussion
    if (intensity >= 2) {
        s += noise() * penv * 0.18f;
        penv -= penv * pdecay / SR;
        if (penv < 0.0f) penv = 0.0f;
    }
    return s;
}

// ---------------- mixer ----------------
static void mixer_task(void *arg)
{
    static int16_t block[MIX_BLOCK];
    while (1) {
        for (int i = 0; i < MIX_BLOCK; i++) {
            float acc = music_sample();

            for (int v = 0; v < MAX_VOICES; v++) {
                voice_t *vo = &voices[v];
                if (!vo->active) continue;
                acc += (vo->data[vo->pos] / 32768.0f) * vo->vol;
                if (++vo->pos >= vo->len) vo->active = false;
            }

            acc *= master;
            // Soft clip so a pile-up of voices distorts gracefully.
            if (acc > 1.0f)       acc = 1.0f;
            else if (acc < -1.0f) acc = -1.0f;
            else                  acc = acc - (acc * acc * acc) / 3.0f;
            block[i] = (int16_t)(acc * 30000.0f);
        }
        audio_write(block, MIX_BLOCK);
    }
}

static void vol_save(void)
{
    nvs_handle_t h;
    if (nvs_open("sc", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i8(h, "vol", (int8_t)vol_level);
    nvs_commit(h);
    nvs_close(h);
}

static void vol_load(void)
{
    nvs_handle_t h;
    if (nvs_open("sc", NVS_READONLY, &h) != ESP_OK) return;
    int8_t v = 2;
    if (nvs_get_i8(h, "vol", &v) == ESP_OK && v >= 0 && v < SFX_LEVELS) vol_level = v;
    nvs_close(h);
}

void sfx_set_level(int lv)
{
    if (lv < 0) lv = 0;
    if (lv >= SFX_LEVELS) lv = SFX_LEVELS - 1;
    vol_level = lv;
    master = vol_table[lv];
    // Cut the amplifier entirely at MUTE: silences idle hiss and saves power.
    audio_amp(lv > 0);
    vol_save();
    ESP_LOGI(TAG, "volume -> %s", vol_names[lv]);
}

void sfx_cycle_down(void)
{
    sfx_set_level(vol_level == 0 ? SFX_LEVELS - 1 : vol_level - 1);
    if (vol_level > 0) sfx_play(SFX_CLICK, 0.9f);
}

int         sfx_level(void)      { return vol_level; }
const char *sfx_level_name(void) { return vol_names[vol_level]; }

bool sfx_init(void *i2c_bus)
{
    if (!audio_init(i2c_bus, PIN_I2S_DOUT, PIN_I2S_DIN)) {
        ESP_LOGE(TAG, "audio_init failed");
        return false;
    }
    audio_volume(88);
    vol_load();
    master = vol_table[vol_level];
    synthesise();
    vlock = xSemaphoreCreateMutex();
    bfreq = lfreq = 220.0f;
    audio_amp(vol_level > 0);
    // Core 1: keeps the mixer off the core running the game loop.
    xTaskCreatePinnedToCore(mixer_task, "sfx_mix", 4096, NULL, 6, NULL, 1);
    ESP_LOGI(TAG, "audio engine running, volume %s", vol_names[vol_level]);
    return true;
}

void sfx_play(sfx_id_t id, float vol)
{
    if (id < 0 || id >= SFX_COUNT || !clips[id].data) return;
    xSemaphoreTake(vlock, portMAX_DELAY);
    int slot = -1, oldest = 0;
    for (int v = 0; v < MAX_VOICES; v++) {
        if (!voices[v].active) { slot = v; break; }
        if (voices[v].pos > voices[oldest].pos) oldest = v;
    }
    if (slot < 0) slot = oldest;                 // steal the most-finished voice
    voices[slot].data = clips[id].data;
    voices[slot].len  = clips[id].len;
    voices[slot].pos  = 0;
    voices[slot].vol  = vol;
    voices[slot].active = true;
    xSemaphoreGive(vlock);
}

void sfx_music(bool on)
{
    music_on = on;
    if (on) { seq_step = 0; step_left = 0; benv = lenv = penv = 0.0f; }
}
void sfx_music_intensity(int level) { intensity = level < 0 ? 0 : (level > 3 ? 3 : level); }
void sfx_master(float v) { master = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
