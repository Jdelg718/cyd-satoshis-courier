#include <math.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "seedui.h"
#include "config.h"
#include "display.h"
#include "touch.h"
#include "sfx.h"
#include "rgbled.h"
#include "entropy.h"
#include "bip39.h"
#include "storage.h"
#include "board.h"
#include "game.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "seedui";

typedef struct { int x, y, w, h; const char *label; uint16_t col; } btn_t;

static bool touch_down(int *x, int *y)
{
    touch_pt_t p;
    if (touch_read(&p) && p.pressed) { *x = p.x; *y = p.y; return true; }
    return false;
}

static void draw_btn(const btn_t *b, float t, bool held)
{
    uint8_t a = held ? 220 : (uint8_t)(110 + 50 * sinf(t * 3.2f));
    disp_blend_rect(b->x, b->y, b->w, b->h, b->col, held ? 190 : 40);
    disp_rect(b->x, b->y, b->w, b->h, b->col);
    disp_rect(b->x + 1, b->y + 1, b->w - 2, b->h - 2, disp_lerp(C_BLACK, b->col, a));
    int tw = disp_text_width(b->label, 2);
    disp_text(b->x + (b->w - tw) / 2, b->y + (b->h - 14) / 2, b->label,
              held ? C_BLACK : C_WHITE, 2);
}

static bool btn_hit(const btn_t *b, int x, int y)
{
    return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

static void header(const char *title, uint16_t col)
{
    disp_vgradient(0, LCD_H, RGB(8, 6, 20), RGB(2, 2, 6));
    disp_fill_rect(0, 0, LCD_W, 26, col);
    int tw = disp_text_width(title, 1);
    disp_text((LCD_W - tw) / 2, 10, title, C_BLACK, 1);
}

static void wait_release(void)
{
    int x, y;
    while (touch_down(&x, &y)) vTaskDelay(pdMS_TO_TICKS(10));
    vTaskDelay(pdMS_TO_TICKS(60));
}

// ---------------- "not enough entropy" ----------------
static void screen_too_short(const run_result_t *r)
{
    btn_t ok = { 60, 250, 120, 36, "OK", C_ORANGE };
    wait_release();
    while (1) {
        float t = esp_timer_get_time() / 1000000.0f;
        header("NOT ENOUGH ENTROPY", C_RED);

        disp_text_center(50, "You flew too well.", C_WHITE, 1);
        disp_text_center(68, "Or not long enough.", C_WHITE, 1);

        char buf[48];
        snprintf(buf, sizeof(buf), "flight %lu.%lus / %ds needed",
                 (unsigned long)(r->run_ms / 1000), (unsigned long)((r->run_ms % 1000) / 100),
                 MIN_TIME_FOR_12WORD_MS / 1000);
        disp_text_center(104, buf, C_DIM, 1);
        snprintf(buf, sizeof(buf), "actions %lu / %d needed",
                 (unsigned long)r->actions, MIN_ACTIONS_FOR_12WORD);
        disp_text_center(120, buf, C_DIM, 1);

        disp_fill_rect(20, 150, LCD_W - 40, 1, RGB(40, 40, 60));
        disp_text_center(166, "Longer, messier flights", C_YELLOW, 1);
        disp_text_center(182, "make better entropy.", C_YELLOW, 1);
        disp_text_center(206, "A perfect run gives nothing.", C_DIM, 1);

        int x, y;
        bool down = touch_down(&x, &y);
        draw_btn(&ok, t, down && btn_hit(&ok, x, y));
        ui_volume_tick();
        disp_flush_async();
        if (down && btn_hit(&ok, x, y)) { sfx_play(SFX_CLICK, 0.8f); wait_release(); return; }
        vTaskDelay(pdMS_TO_TICKS(8));
    }
}

// ---------------- generating animation ----------------
static void screen_generating(int words)
{
    for (int f = 0; f < 70; f++) {
        float t = f / 60.0f;
        header("HARVESTING ENTROPY", C_ORANGE);
        char buf[40];
        snprintf(buf, sizeof(buf), "%d WORDS", words);
        disp_text_center(120, buf, C_ORANGE, 3);

        for (int i = 0; i < 26; i++) {
            float a = t * 3.0f + i * 0.24f;
            int rr = 46 + (int)(13 * sinf(t * 5.0f + i));
            disp_glow(LCD_W / 2 + (int)(cosf(a) * rr), 190 + (int)(sinf(a) * rr * 0.42f),
                      4, i % 3 ? C_ORANGE : C_CYAN, 150);
        }
        int w = (int)(200.0f * (f / 69.0f));
        disp_fill_rect(20, 250, 200, 5, RGB(30, 30, 44));
        disp_fill_rect(20, 250, w, 5, C_ORANGE);
        disp_text_center(268, entropy_sources(), C_DIM, 1);
        ui_volume_tick();
        disp_flush_async();
        rgbled_set((uint8_t)(60 + 60 * sinf(t * 6)), 30, 0);
        vTaskDelay(pdMS_TO_TICKS(8));
    }
}

// ---------------- seed display ----------------
#define PER_PAGE 6

static int screen_show_seed(const mnemonic_t *m)
{
    int page = 0;
    const int pages = (m->n_words + PER_PAGE - 1) / PER_PAGE;
    btn_t next = { 128, 268, 104, 36, "NEXT", C_CYAN };
    btn_t save = { 128, 268, 104, 36, "SAVE", C_GREEN };
    btn_t done = { 8,   268, 104, 36, "DONE", C_ORANGE };
    wait_release();

    while (1) {
        float t = esp_timer_get_time() / 1000000.0f;
        bool last = (page == pages - 1);

        header("DEMO SEED - NOT FOR REAL FUNDS", C_RED);

        char buf[48];
        snprintf(buf, sizeof(buf), "%d words   page %d/%d", m->n_words, page + 1, pages);
        disp_text_center(34, buf, C_DIM, 1);

        int y = 56;
        for (int i = 0; i < PER_PAGE; i++) {
            int idx = page * PER_PAGE + i;
            if (idx >= m->n_words) break;
            snprintf(buf, sizeof(buf), "%2d", idx + 1);
            disp_text(10, y + 3, buf, C_DIM, 1);
            disp_fill_rect(30, y - 2, 2, 20, C_ORANGE);
            disp_text_glow(40, y, m->word[idx], C_WHITE, 2);
            y += 34;
        }

        disp_blend_rect(0, 238, LCD_W, 22, C_RED, 45);
        disp_text_center(244, "write it down - demo only", RGB(255, 140, 140), 1);

        int x, ty;
        bool down = touch_down(&x, &ty);
        draw_btn(&done, t, down && btn_hit(&done, x, ty));
        const btn_t *rb = last ? &save : &next;
        draw_btn(rb, t, down && btn_hit(rb, x, ty));
        ui_volume_tick();
        disp_flush_async();

        if (down) {
            if (btn_hit(&done, x, ty)) { sfx_play(SFX_CLICK, 0.8f); wait_release(); return 0; }
            if (btn_hit(rb, x, ty)) {
                sfx_play(SFX_CLICK, 0.8f);
                wait_release();
                if (last) return 1;            // user asked to save
                page++;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(8));
    }
}

// ---------------- guarded SD write ----------------
static bool next_seed_path(char *out, size_t n)
{
    for (int i = 1; i < 10000; i++) {
        snprintf(out, n, SD_MOUNT_POINT "/seed%04d.txt", i);
        FILE *f = fopen(out, "r");
        if (!f) return true;
        fclose(f);
    }
    return false;
}

static char last_err[48] = "";

static bool write_seed_file(const mnemonic_t *m, const run_result_t *r, char *path_out, size_t pn)
{
    last_err[0] = 0;
    if (!next_seed_path(path_out, pn)) {
        snprintf(last_err, sizeof(last_err), "no free filename");
        return false;
    }
    FILE *f = fopen(path_out, "w");
    if (!f) {
        snprintf(last_err, sizeof(last_err), "open: %s", strerror(errno));
        ESP_LOGE(TAG, "fopen %s failed: %s (errno %d)", path_out, strerror(errno), errno);
        return false;
    }

    uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000);
    fprintf(f,
        "================================================\n"
        " DEMO ONLY - Entropy harvested from game play.\n"
        " Not suitable for real Bitcoin storage.\n"
        " Verify independently before any use.\n"
        "================================================\n\n"
        "Satoshi's Courier - ESP32-S3 entropy demo\n\n"
        "MNEMONIC (%d words, %d-bit entropy):\n%s\n\n",
        m->n_words, m->ent_bits, m->joined);

    for (int i = 0; i < m->n_words; i++) fprintf(f, "%2d. %s\n", i + 1, m->word[i]);

    fprintf(f,
        "\n--- run ---\n"
        "score            : %lu\n"
        "sats collected   : %lu\n"
        "depth            : %lu m\n"
        "flight time      : %lu ms\n"
        "player actions   : %lu\n"
        "best combo       : x%lu\n"
        "gameplay entropy : ~%lu bits (estimated)\n"
        "entropy sources  : %s\n"
        "device uptime    : %lu s\n"
        "\nThis file contains the seed in PLAINTEXT.\n"
        "Delete it when you are done experimenting.\n",
        (unsigned long)r->score, (unsigned long)r->sats, (unsigned long)r->depth_m,
        (unsigned long)r->run_ms, (unsigned long)r->actions, (unsigned long)r->best_combo,
        (unsigned long)entropy_play_bits(), entropy_sources(), (unsigned long)up);

    bool ok = (fclose(f) == 0);
    if (!ok) snprintf(last_err, sizeof(last_err), "close: %s", strerror(errno));
    ESP_LOGI(TAG, "wrote %s (%s)", path_out, ok ? "ok" : last_err);
    return ok;
}

static void screen_result(const char *l1, const char *l2, uint16_t col, bool good)
{
    btn_t ok = { 60, 250, 120, 36, "OK", col };
    wait_release();
    rgbled_set(good ? 0 : 90, good ? 80 : 0, 0);
    while (1) {
        float t = esp_timer_get_time() / 1000000.0f;
        header(good ? "SAVED" : "NOT SAVED", col);
        disp_text_center(120, l1, col, 2);
        if (l2) disp_text_center(160, l2, C_DIM, 1);
        int x, y;
        bool down = touch_down(&x, &y);
        draw_btn(&ok, t, down && btn_hit(&ok, x, y));
        ui_volume_tick();
        disp_flush_async();
        if (down && btn_hit(&ok, x, y)) { sfx_play(SFX_CLICK, 0.8f); wait_release(); return; }
        vTaskDelay(pdMS_TO_TICKS(8));
    }
}

static void screen_confirm_save(const mnemonic_t *m, const run_result_t *r)
{
    if (!sd_present()) {
        screen_result("NO SD CARD", "insert a card and run again", C_RED, false);
        return;
    }
    if (!sd_writable()) {
        screen_result("CARD NOT WRITABLE", "locked, full or read-only", C_RED, false);
        return;
    }

    btn_t hold   = { 30, 236, 180, 44, "HOLD TO WRITE", C_RED };
    btn_t cancel = { 70, 288, 100, 26, "CANCEL", C_DIM };
    int64_t hold_start = 0;
    wait_release();

    while (1) {
        float t = esp_timer_get_time() / 1000000.0f;
        header("PLAINTEXT WRITE WARNING", C_RED);

        disp_text_center(44,  "This writes the seed", C_WHITE, 1);
        disp_text_center(60,  "to the card IN PLAINTEXT.", C_WHITE, 1);
        disp_text_center(86,  "Anyone with the card", RGB(255, 150, 150), 1);
        disp_text_center(102, "can read it.", RGB(255, 150, 150), 1);

        disp_fill_rect(24, 124, LCD_W - 48, 1, RGB(60, 30, 30));
        char buf[48];
        snprintf(buf, sizeof(buf), "%s  %lluMB", sd_name(), (unsigned long long)sd_capacity_mb());
        disp_text_center(138, buf, C_DIM, 1);
        snprintf(buf, sizeof(buf), "%d-word demo seed", m->n_words);
        disp_text_center(156, buf, C_ORANGE, 1);
        disp_text_center(182, "Confirm only if you", C_DIM, 1);
        disp_text_center(198, "understand the risk.", C_DIM, 1);

        int x, y;
        bool down = touch_down(&x, &y);
        bool on_hold = down && btn_hit(&hold, x, y);

        if (on_hold) {
            if (!hold_start) hold_start = esp_timer_get_time();
        } else {
            hold_start = 0;
        }

        float prog = 0.0f;
        if (hold_start) {
            prog = (esp_timer_get_time() - hold_start) / (float)(SD_CONFIRM_HOLD_MS * 1000);
            if (prog > 1.0f) prog = 1.0f;
        }

        draw_btn(&hold, t, on_hold);
        // Progress fill makes the deliberate 2 s hold legible rather than mysterious.
        if (prog > 0.0f) {
            disp_blend_rect(hold.x + 2, hold.y + 2, (int)((hold.w - 4) * prog), hold.h - 4, C_RED, 200);
            int tw = disp_text_width(hold.label, 2);
            disp_text(hold.x + (hold.w - tw) / 2, hold.y + (hold.h - 14) / 2, hold.label, C_WHITE, 2);
        }
        draw_btn(&cancel, t, down && btn_hit(&cancel, x, y));
        ui_volume_tick();
        disp_flush_async();

        if (prog >= 1.0f) {
            char path[64];
            bool ok = write_seed_file(m, r, path, sizeof(path));
            sfx_play(ok ? SFX_SEED : SFX_DENY, 1.0f);
            const char *base = strrchr(path, '/');
            screen_result(ok ? (base ? base + 1 : path) : "WRITE FAILED",
                          ok ? "plaintext - delete when done" : last_err,
                          ok ? C_GREEN : C_RED, ok);
            return;
        }
        if (down && btn_hit(&cancel, x, y)) {
            sfx_play(SFX_CLICK, 0.8f);
            wait_release();
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(8));
    }
}

// ---------------- entry point ----------------
void seedui_run(const run_result_t *r)
{
    sfx_music(false);

    int words = 0;
    size_t ent_len = 0;
    if (r->run_ms >= MIN_TIME_FOR_24WORD_MS && r->actions >= MIN_ACTIONS_FOR_24WORD) {
        words = 24; ent_len = 32;
    } else if (r->run_ms >= MIN_TIME_FOR_12WORD_MS && r->actions >= MIN_ACTIONS_FOR_12WORD) {
        words = 12; ent_len = 16;
    }

    if (!words) { screen_too_short(r); return; }

    screen_generating(words);

    uint8_t ent[32];
    mnemonic_t m;
    if (!entropy_generate(ent, ent_len) || !bip39_from_entropy(ent, ent_len, &m)) {
        memset(ent, 0, sizeof(ent));
        screen_result("GENERATION FAILED", "entropy pool error", C_RED, false);
        return;
    }
    memset(ent, 0, sizeof(ent));              // entropy is no longer needed
    sfx_play(SFX_SEED, 0.9f);
    rgbled_set(0, 90, 20);
    ESP_LOGI(TAG, "generated %d-word demo seed (%d-bit)", m.n_words, m.ent_bits);

    if (screen_show_seed(&m) == 1) screen_confirm_save(&m, r);

    memset(&m, 0, sizeof(m));                 // do not leave the phrase in RAM
    rgbled_set(20, 10, 0);
}

#ifdef SEED_WRITE_TEST
bool seedui_write_test(void)
{
    if (!sd_present()) { ESP_LOGE(TAG, "WRITE TEST: no card"); return false; }

    // Known BIP-39 vector so the file contents are predictable.
    uint8_t ent[16] = {0};
    mnemonic_t m;
    if (!bip39_from_entropy(ent, sizeof(ent), &m)) { ESP_LOGE(TAG, "WRITE TEST: mnemonic failed"); return false; }

    run_result_t r = { .score = 1234, .sats = 56, .depth_m = 78,
                       .run_ms = 91000, .actions = 123, .best_combo = 7 };

    char path[64];
    bool ok = write_seed_file(&m, &r, path, sizeof(path));
    if (!ok) { ESP_LOGE(TAG, "WRITE TEST: write failed: %s", last_err); return false; }
    ESP_LOGW(TAG, "WRITE TEST: wrote %s", path);

    FILE *f = fopen(path, "r");
    if (!f) { ESP_LOGE(TAG, "WRITE TEST: reopen failed: %s", strerror(errno)); return false; }
    char line[128];
    int n = 0;
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (n < 12) ESP_LOGW(TAG, "  | %s", line);
        if (strstr(line, m.joined)) found = true;
        n++;
    }
    fclose(f);
    ESP_LOGW(TAG, "WRITE TEST: %d lines, mnemonic round-trip %s", n, found ? "OK" : "MISSING");
    remove(path);
    ESP_LOGW(TAG, "WRITE TEST: cleaned up, result %s", (found && n > 20) ? "PASS" : "FAIL");
    return found && n > 20;
}
#endif
