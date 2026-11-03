#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "audio.h"
#include "es8311.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "audio";
static i2s_chan_handle_t tx_chan = NULL;
static i2s_chan_handle_t rx_chan = NULL;
static es8311_handle_t codec = NULL;
static int cur_vol = 70;

void audio_amp(bool on)
{
    // FM8002E enable is ACTIVE LOW.
    gpio_set_level(PIN_AMP_EN, on ? 0 : 1);
}

bool audio_init(void *i2c_bus, int dout_pin, int din_pin)
{
    gpio_config_t amp = { .pin_bit_mask = 1ULL << PIN_AMP_EN, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&amp);
    audio_amp(false);

    // ---- I2S master, provides MCLK = 256 * fs (matches ES8311 coeff table) ----
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.dma_desc_num  = 6;
    cc.dma_frame_num = 240;
    cc.auto_clear    = true;
    if (i2s_new_channel(&cc, &tx_chan, &rx_chan) != ESP_OK) { ESP_LOGE(TAG, "i2s_new_channel failed"); return false; }

    i2s_std_config_t sc = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = PIN_I2S_MCLK,
            .bclk = PIN_I2S_BCLK,
            .ws   = PIN_I2S_LRCK,
            .dout = dout_pin,
            .din  = din_pin,
            .invert_flags = { false, false, false },
        },
    };
    sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    if (i2s_channel_init_std_mode(tx_chan, &sc) != ESP_OK) { ESP_LOGE(TAG, "i2s tx std init failed"); return false; }
    if (i2s_channel_init_std_mode(rx_chan, &sc) != ESP_OK) { ESP_LOGE(TAG, "i2s rx std init failed"); return false; }
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));

    // ---- ES8311 codec over the shared I2C bus ----
    codec = es8311_create(i2c_bus, ES8311_ADDR);
    if (!codec) { ESP_LOGE(TAG, "es8311_create failed (addr 0x%02X)", ES8311_ADDR); return false; }

    const es8311_clock_config_t clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = AUDIO_SAMPLE_RATE * 256,
        .sample_frequency = AUDIO_SAMPLE_RATE,
    };
    if (es8311_init(codec, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {
        ESP_LOGE(TAG, "es8311_init failed"); return false;
    }
    es8311_voice_volume_set(codec, cur_vol, NULL);
    es8311_microphone_config(codec, false);          // analog MEMS mic
    es8311_microphone_gain_set(codec, ES8311_MIC_GAIN_24DB);
    ESP_LOGI(TAG, "ES8311 up @ %d Hz, DOUT=IO%d DIN=IO%d", AUDIO_SAMPLE_RATE, dout_pin, din_pin);
    return true;
}

void audio_deinit(void)
{
    audio_amp(false);
    if (tx_chan) { i2s_channel_disable(tx_chan); i2s_del_channel(tx_chan); tx_chan = NULL; }
    if (rx_chan) { i2s_channel_disable(rx_chan); i2s_del_channel(rx_chan); rx_chan = NULL; }
    if (codec)   { es8311_delete(codec); codec = NULL; }
    // i2s_del_channel does NOT detach pins from the GPIO matrix. Without this,
    // a pin configured as DOUT stays wired to the I2S TX signal and keeps
    // producing audio after a reconfigure - which invalidates any A/B pin test.
    gpio_reset_pin(PIN_I2S_MCLK);
    gpio_reset_pin(PIN_I2S_BCLK);
    gpio_reset_pin(PIN_I2S_LRCK);
    gpio_reset_pin(GPIO_NUM_6);
    gpio_reset_pin(GPIO_NUM_8);
}

void audio_volume(int pct)
{
    cur_vol = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    if (codec) es8311_voice_volume_set(codec, cur_vol, NULL);
}

size_t audio_write(const int16_t *mono, size_t n)
{
    if (!tx_chan) return 0;
    // Duplicate mono into the stereo frame the codec expects.
    static int16_t st[512];
    size_t done = 0;
    while (done < n) {
        size_t chunk = n - done; if (chunk > 256) chunk = 256;
        for (size_t i = 0; i < chunk; i++) { st[i * 2] = mono[done + i]; st[i * 2 + 1] = mono[done + i]; }
        size_t wrote = 0;
        i2s_channel_write(tx_chan, st, chunk * 4, &wrote, portMAX_DELAY);
        done += chunk;
    }
    return done;
}

void audio_tone(float freq, int ms, float amp)
{
    const int n = AUDIO_SAMPLE_RATE / 100;              // 10 ms blocks
    int16_t *buf = malloc(n * sizeof(int16_t));
    if (!buf) return;
    int blocks = ms / 10;
    float ph = 0.0f, step = 2.0f * (float)M_PI * freq / AUDIO_SAMPLE_RATE;
    audio_amp(true);
    for (int b = 0; b < blocks; b++) {
        // Short attack/release so the speaker does not click.
        for (int i = 0; i < n; i++) {
            float env = 1.0f;
            int pos = b * n + i, total = blocks * n;
            if (pos < 441) env = pos / 441.0f;
            else if (pos > total - 441) env = (total - pos) / 441.0f;
            buf[i] = (int16_t)(sinf(ph) * 32000.0f * amp * env);
            ph += step; if (ph > 2.0f * (float)M_PI) ph -= 2.0f * (float)M_PI;
        }
        audio_write(buf, n);
    }
    free(buf);
}


size_t audio_read(int16_t *mono, size_t n)
{
    if (!rx_chan) return 0;
    static int16_t st[512];
    size_t done = 0;
    while (done < n) {
        size_t chunk = n - done; if (chunk > 256) chunk = 256;
        size_t got = 0;
        if (i2s_channel_read(rx_chan, st, chunk * 4, &got, pdMS_TO_TICKS(300)) != ESP_OK) break;
        size_t frames = got / 4;
        for (size_t i = 0; i < frames; i++) mono[done + i] = st[i * 2];   // left == mono mic
        done += frames;
        if (frames == 0) break;
    }
    return done;
}

// Goertzel magnitude at one frequency. Narrowband on purpose: a floating input
// pin produces broadband noise, which a plain RMS check would misread as a pass.
static float goertzel(const int16_t *x, size_t n, float freq)
{
    const float w = 2.0f * (float)M_PI * freq / AUDIO_SAMPLE_RATE;
    const float coeff = 2.0f * cosf(w);
    float s0, s1 = 0.0f, s2 = 0.0f;
    for (size_t i = 0; i < n; i++) {
        s0 = (x[i] / 32768.0f) + coeff * s1 - s2;
        s2 = s1; s1 = s0;
    }
    float mag2 = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    if (mag2 < 0.0f) mag2 = 0.0f;
    return sqrtf(mag2) / (float)n;
}

float audio_loopback_probe(float freq, int ms)
{
    if (!tx_chan || !rx_chan) return -1.0f;

    const size_t BLK = 441;                   // exactly 10 periods of 1 kHz @ 44.1 kHz
    int16_t *tone = malloc(BLK * sizeof(int16_t));
    int16_t *rec  = malloc(BLK * sizeof(int16_t));
    if (!tone || !rec) { free(tone); free(rec); return -1.0f; }
    for (size_t i = 0; i < BLK; i++)
        tone[i] = (int16_t)(sinf(2.0f * (float)M_PI * freq * i / AUDIO_SAMPLE_RATE) * 29000.0f);

    // --- baseline: amplifier off, nothing transmitted ---
    audio_amp(false);
    vTaskDelay(pdMS_TO_TICKS(120));
    for (int i = 0; i < 3; i++) audio_read(rec, BLK);          // discard stale DMA
    float quiet = 0.0f;
    for (int i = 0; i < 6; i++) { audio_read(rec, BLK); quiet += goertzel(rec, BLK, freq); }
    quiet /= 6.0f;

    // --- active: play the tone and listen at the same time ---
    audio_amp(true);
    int blocks = (ms * AUDIO_SAMPLE_RATE / 1000) / BLK;
    if (blocks < 8) blocks = 8;
    float loud = 0.0f;
    int counted = 0;
    for (int b = 0; b < blocks; b++) {
        audio_write(tone, BLK);
        audio_read(rec, BLK);
        if (b >= blocks / 3) { loud += goertzel(rec, BLK, freq); counted++; }  // let it settle
    }
    if (counted) loud /= counted;
    audio_amp(false);

    free(tone); free(rec);
    ESP_LOGI(TAG, "loopback @ %.0f Hz: quiet=%.6f loud=%.6f", freq, quiet, loud);
    if (quiet <= 1e-9f) quiet = 1e-9f;
    return loud / quiet;
}
