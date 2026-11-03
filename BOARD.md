# ES3C28P — verified hardware notes

Everything below was confirmed on the physical board, not read off a datasheet.
Date: 2026-11-02. Toolchain: PlatformIO 6.2.0 + pioarduino espressif32 55.03.312 + ESP-IDF 5.5.5.

## Silicon (read over USB with esptool)
| | |
|---|---|
| Chip | ESP32-S3 (QFN56) rev **v0.2**, dual core + LP core, 240 MHz |
| PSRAM | **8 MB octal**, reported by IDF as 8192 KB |
| Flash | **16 MB quad** (eFuse: quad, 3.3 V) |
| Port | `/dev/ttyACM0`, native USB-JTAG/CDC, VID:PID `303a:1001` |
| MAC | `84:c7:bb:79:2d:f4` |
| PlatformIO board | `esp32-s3-devkitc1-n16r8` |

## Verified peripherals
| Peripheral | Status | Measured |
|---|---|---|
| ILI9341V 240×320 | PASS | init 359 ms; colour order correct with **no BGR swap**; portrait, origin top-left |
| Full-frame DMA from PSRAM | PASS | **16.5 ms/frame = 60 fps** (240×320×16bpp @ 80 MHz SPI ≈ 15.4 ms of pure bandwidth — we are at the bus limit) |
| FT6336G touch | PASS | vendor id `0x11`, chip id `0x64`; **raw coords map 1:1 to screen space, unmirrored** (corner deltas +2..+28 px = finger centre offset only) |
| ES8311 codec | PASS | I2C handshake OK, "Slave mode and I2S format", 44.1 kHz, MCLK = 256×fs |
| microSD | PASS | `NCard` 14910 MB, SDIO **4-bit** |
| WS2812B RGB LED | PASS | IO42, GRB order |
| ES8311 microphone | PASS | analog MEMS via codec ADC; 1 kHz tone detected at 0.029 vs 0.000008 noise floor |
| **I2S pinout** | **RESOLVED** | **DOUT=IO8, DIN=IO6** (3588.9x vs 0.0x) |
| Battery ADC (IO9) | PASS | reads 3.63–3.64 V |
| IO2 floating ADC | PASS | **378–393 LSB of jitter** per 64 samples — a genuine entropy source |

## Gotchas found the hard way
- **LCD reset is tied to chip EN.** Configure panel reset as `-1`; the display cannot be
  reset independently of the SoC.
- **IO45 (backlight) and IO46 (LCD DC) are ESP32-S3 strapping pins.** Works as shipped.
- **FM8002E amplifier enable on IO1 is ACTIVE LOW.** Drive low to get sound.
- **`i2s_del_channel()` does not detach pins from the GPIO matrix.** A pin configured as
  DOUT keeps driving the I2S TX signal after teardown, so an A/B pin test gives a false
  positive on the second attempt unless you `gpio_reset_pin()` each one. This is what made
  the first DOUT/DIN test report that both IO6 and IO8 worked.
- **An empty SD slot fails as `sdmmc_init_ocr: send_op_cond returned 0x107` (ESP_ERR_TIMEOUT).**
  That is not a wiring fault — check for a card first.
- **I2S DOUT is IO8 and DIN is IO6 — measured, not inferred.** The vendor (lcdwiki) table is
  literally correct; the ngttai BSP table has these swapped. Resolved by a mic-loopback
  self-test: the firmware plays 1 kHz and Goertzel-detects it on the onboard MEMS mic, so
  only the pairing that physically closes the acoustic loop scores. Result was not marginal:

  | config | quiet | loud | ratio |
  |---|---|---|---|
  | DOUT=IO6 DIN=IO8 | 0.000000 | 0.000000 | **0.0x** |
  | DOUT=IO8 DIN=IO6 | 0.000008 | 0.029233 | **3588.9x** |

  Re-run any time with the bring-up firmware; it re-derives this on every boot.

- **ESP-IDF defaults FATFS to `CONFIG_FATFS_LFN_NONE` (8.3 filenames only).** A basename
  longer than 8 characters makes `fopen(..., "w")` return NULL on a perfectly healthy,
  freshly mounted card — `seed_0001.txt` fails, `seed0001.txt` works. Enable
  `CONFIG_FATFS_LFN_HEAP` for real names. Mounting a card proves nothing about whether
  writes will succeed, so the firmware now runs an explicit create/write/close/delete
  probe at boot and reports `rw` or `RO!` on the splash screen.

## Upstream sources
- https://www.lcdwiki.com/2.8inch_ESP32-S3_Display
- https://github.com/ngttai/esp32_s3_es3c28p
- ES8311 driver vendored from espressif/esp-bsp (Apache-2.0) — see `components/es8311/VENDORED.md`
