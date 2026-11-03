#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "storage.h"
#include "board.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

static const char *TAG = "sd";
static sdmmc_card_t *card = NULL;

bool sd_mount(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    host.slot = SDMMC_HOST_SLOT_1;

    // ESP32-S3 routes SDMMC through the GPIO matrix, so pins are explicit.
    sdmmc_slot_config_t slot = {
        .clk   = PIN_SD_CLK,
        .cmd   = PIN_SD_CMD,
        .d0    = PIN_SD_D0,
        .d1    = PIN_SD_D1,
        .d2    = PIN_SD_D2,
        .d3    = PIN_SD_D3,
        .cd    = SDMMC_SLOT_NO_CD,
        .wp    = SDMMC_SLOT_NO_WP,
        .width = 4,
        .flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP,
    };

    esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mcfg, &card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mount failed: %s (card absent or unformatted?)", esp_err_to_name(err));
        card = NULL;
        return false;
    }
    ESP_LOGI(TAG, "mounted '%s' %lluMB %s", card->cid.name,
             ((uint64_t)card->csd.capacity * card->csd.sector_size) >> 20,
             card->is_sdio ? "SDIO" : (card->is_mmc ? "MMC" : "SD"));
    return true;
}

void sd_unmount(void)
{
    if (card) { esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, card); card = NULL; }
}

bool sd_present(void) { return card != NULL; }
uint64_t sd_capacity_mb(void)
{
    return card ? (((uint64_t)card->csd.capacity * card->csd.sector_size) >> 20) : 0;
}
const char *sd_name(void) { return card ? card->cid.name : "none"; }

bool sd_writable(void)
{
    if (!card) return false;
    const char *probe = SD_MOUNT_POINT "/.scwtest";
    FILE *f = fopen(probe, "w");
    if (!f) {
        ESP_LOGW(TAG, "write probe failed to open: %s (errno %d)", strerror(errno), errno);
        return false;
    }
    bool ok = (fputs("satoshis courier write probe\n", f) >= 0);
    if (fclose(f) != 0) ok = false;
    remove(probe);
    ESP_LOGI(TAG, "write probe: %s", ok ? "writable" : "FAILED");
    return ok;
}
