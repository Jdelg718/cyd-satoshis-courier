idf_component_register(
    SRCS "main.c" "board.c" "display.c" "touch.c" "audio.c" "storage.c" "rgbled.c" "sfx.c" "game.c" "entropy.c" "bip39.c" "seedui.c"
    INCLUDE_DIRS "." "../include"
    REQUIRES driver esp_lcd esp_driver_i2c esp_driver_i2s esp_driver_sdmmc
             esp_driver_spi esp_driver_gpio esp_adc fatfs esp_timer esp_driver_rmt
             mbedtls bootloader_support esp_hw_support nvs_flash)
