// Satoshi's Courier - ESP32-S3 ES3C28P
// Boot, self-test, then hand the machine to the game loop.
#include <stdio.h>
#include "board.h"
#include "display.h"
#include "touch.h"
#include "sfx.h"
#include "storage.h"
#include "rgbled.h"
#include "entropy.h"
#include "bip39.h"
#include "game.h"
#include "seedui.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_chip_info.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

static void boot_line(int y, const char *label, const char *val, uint16_t col)
{
    disp_text(14, y, label, C_DIM, 1);
    disp_text(120, y, val, col, 1);
    disp_flush();
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Satoshi's Courier ===");

    esp_chip_info_t ci; esp_chip_info(&ci);
    ESP_LOGI(TAG, "ESP32-S3 rev v%d.%d, PSRAM %u KB",
             ci.revision / 100, ci.revision % 100,
             (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024));

    esp_err_t nv = nvs_flash_init();
    if (nv == ESP_ERR_NVS_NO_FREE_PAGES || nv == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    board_init();
    rgbled_init();
    rgbled_set(30, 12, 0);
    disp_init();

    // --- boot screen doubles as the self-test report ---
    disp_vgradient(0, LCD_H, RGB(10, 8, 26), RGB(2, 2, 8));
    disp_fill_circle(LCD_W / 2, 70, 26, C_ORANGE);
    disp_fill_circle(LCD_W / 2, 70, 22, RGB(255, 190, 70));
    disp_text(LCD_W / 2 - 5, 62, "S", RGB(120, 60, 0), 3);
    disp_text_center(112, "SATOSHIS COURIER", C_WHITE, 2);
    disp_text_center(136, "booting", C_DIM, 1);
    disp_flush();

    int y = 170;
    char buf[48];

    snprintf(buf, sizeof(buf), "%u KB", (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024));
    boot_line(y, "psram", buf, C_GREEN); y += 16;

    touch_init();
    uint8_t vid = touch_probe_id();
    boot_line(y, "touch", vid ? "FT6336G" : "ABSENT", vid ? C_GREEN : C_RED); y += 16;

    bool sd = sd_mount();
    bool sdw = sd && sd_writable();
    if (sd)  snprintf(buf, sizeof(buf), "%lluMB %s", (unsigned long long)sd_capacity_mb(),
                      sdw ? "rw" : "RO!");
    else     snprintf(buf, sizeof(buf), "no card");
    boot_line(y, "sdcard", buf, !sd ? C_YELLOW : (sdw ? C_GREEN : C_RED)); y += 16;

    bool au = sfx_init(touch_i2c_bus());
    if (au) snprintf(buf, sizeof(buf), "ES8311 %s", sfx_level_name());
    else    snprintf(buf, sizeof(buf), "FAILED");
    boot_line(y, "audio", buf, au ? C_GREEN : C_RED); y += 16;

    bool en = entropy_init();
    boot_line(y, "entropy", en ? "TRNG+DRBG" : "FAILED", en ? C_GREEN : C_RED); y += 16;

    // Spec §9.4: prove the BIP-39 conversion against the official vectors
    // before the player is ever shown a phrase.
    bool bip = bip39_selftest();
    boot_line(y, "bip39", bip ? "VECTORS OK" : "VECTORS FAIL", bip ? C_GREEN : C_RED); y += 16;

    if (!bip || !en) {
        disp_text_center(y + 16, "SELF-TEST FAILED", C_RED, 2);
        disp_text_center(y + 40, "seed generation disabled", C_DIM, 1);
        disp_flush();
        rgbled_set(120, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(6000));
    }

#ifdef SEED_WRITE_TEST
    seedui_write_test();
#endif

    sfx_play(SFX_LEVELUP, 0.5f);
    vTaskDelay(pdMS_TO_TICKS(900));

    game_init();
    game_run();            // never returns
}
