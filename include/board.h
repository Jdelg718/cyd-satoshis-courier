// board.h - ES3C28P : ESP32-S3-N16R8 + 2.8" ILI9341V + FT6336G + ES8311/FM8002E
// Verified against vendor docs (lcdwiki) + ngttai/esp32_s3_es3c28p BSP.
// Silicon confirmed on-device: ESP32-S3 rev v0.2, 16MB quad flash, 8MB octal PSRAM.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

// ---------------- Display: ILI9341V, 4-wire SPI ----------------
#define LCD_SPI_HOST        SPI2_HOST
#define PIN_LCD_CS          GPIO_NUM_10
#define PIN_LCD_DC          GPIO_NUM_46   // NOTE: ESP32-S3 strapping pin
#define PIN_LCD_SCK         GPIO_NUM_12
#define PIN_LCD_MOSI        GPIO_NUM_11
#define PIN_LCD_MISO        GPIO_NUM_13
#define PIN_LCD_BL          GPIO_NUM_45   // NOTE: ESP32-S3 strapping pin (VDD_SPI)
#define PIN_LCD_RST         (-1)          // tied to chip EN - no independent reset
#define LCD_W               240
#define LCD_H               320
#define LCD_SPI_HZ          (80 * 1000 * 1000)

// ---------------- Touch: FT6336G, I2C (shared bus) ----------------
#define TOUCH_I2C_PORT      I2C_NUM_0
#define PIN_I2C_SDA         GPIO_NUM_16
#define PIN_I2C_SCL         GPIO_NUM_15
#define PIN_TOUCH_RST       GPIO_NUM_18
#define PIN_TOUCH_INT       GPIO_NUM_17
#define I2C_BUS_HZ          (400 * 1000)
#define FT6336_ADDR         0x38

// ---------------- Audio: ES8311 codec + FM8002E amp, I2S ----------------
#define PIN_AMP_EN          GPIO_NUM_1    // FM8002E enable - ACTIVE LOW
#define PIN_I2S_MCLK        GPIO_NUM_4
#define PIN_I2S_BCLK        GPIO_NUM_5
#define PIN_I2S_LRCK        GPIO_NUM_7
// CONFIRMED ON HARDWARE by mic-loopback self-test (2026-11-02):
// DOUT=IO8 scored 3588.9x tone-over-noise, DOUT=IO6 scored 0.0x.
// The lcdwiki table was literally correct; the ngttai BSP table is wrong here.
#ifndef PIN_I2S_DOUT
#define PIN_I2S_DOUT        GPIO_NUM_8   // MCU -> codec DSDIN  (speaker)
#endif
#ifndef PIN_I2S_DIN
#define PIN_I2S_DIN         GPIO_NUM_6   // codec DSDOUT -> MCU (microphone)
#endif
#define ES8311_ADDR         0x18          // alt 0x19 if CE strapped high
#define AUDIO_SAMPLE_RATE   44100

// ---------------- microSD: SDIO 4-bit ----------------
#define PIN_SD_CLK          GPIO_NUM_38
#define PIN_SD_CMD          GPIO_NUM_40
#define PIN_SD_D0           GPIO_NUM_39
#define PIN_SD_D1           GPIO_NUM_41
#define PIN_SD_D2           GPIO_NUM_48
#define PIN_SD_D3           GPIO_NUM_47
#define SD_MOUNT_POINT      "/sdcard"

// ---------------- Misc ----------------
#define PIN_RGB_LED         GPIO_NUM_42   // WS2812B, single pixel
#define PIN_BAT_ADC         GPIO_NUM_9    // ADC1_CH8, battery divider
#define PIN_BOOT_BTN        GPIO_NUM_0    // usable in-app as a physical button

// Free expansion header (1.25mm 4P) - floating, usable as ADC entropy taps.
#define PIN_EXP_IO2         GPIO_NUM_2    // ADC1_CH1
#define PIN_EXP_IO3         GPIO_NUM_3    // ADC1_CH2
#define PIN_EXP_IO14        GPIO_NUM_14   // ADC2_CH3
#define PIN_EXP_IO21        GPIO_NUM_21

#include "hal/adc_types.h"

void  board_init(void);
bool  board_boot_pressed(void);
int   board_adc_raw(adc_channel_t ch);
float board_battery_volts(void);
