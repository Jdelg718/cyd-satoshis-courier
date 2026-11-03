# Satoshi's Courier — ESP32-S3 2.8" Display Game Spec

**Target hardware:** ESP32-S3 based "Cheap Yellow Display" style board (or compatible 2.8" 240×320 TFT with microSD).  
**Goal:** Fun Bitcoin-themed arcade-style game whose player inputs generate entropy. Sessions that last long enough produce a BIP39 seed phrase (12-word at 128 bits, 24-word at 256 bits) that can optionally be saved to the onboard microSD card.

**Important:** This is a demo / educational experiment. The entropy is harvested from human play timing + hardware noise sources. It is **not** a security-audited wallet seed generator. Treat any generated seed as demo-grade only. Do not fund real coins with it without independent air-gapped verification and additional entropy.

---

## 1. Game Concept

**Title:** Satoshi's Courier

You are a courier racing through a simple grid or side-scrolling corridor collecting Satoshi coin sprites while dodging obstacles or racing a timer. High-contrast monochrome-or-limited-color graphics (yellow-on-black or bright primary colors work well on the cheap panels).

**Core loop:**
- Player moves a small sprite (arrows, D-pad style touch zones, or on-screen buttons).
- Collect coin sprites → score increases.
- Obstacles or a countdown timer end the run.
- Play style (timing of moves, hesitations, path choices) is the entropy source.

**Meta joke:** Longer / "worse" play (more actions, more time) produces more entropy. A perfect speedrun may produce nothing.

---

## 2. Entropy Harvesting

Collect entropy continuously during play from multiple sources:

1. **Action timestamps**  
   On every significant player action (direction change, button press, touch event):
   - Record `esp_timer_get_time()` (microsecond resolution) or `millis()` + high-resolution timer.
   - Compute delta from previous action.
   - Feed the lower bits of the delta into the entropy pool.

2. **Hardware noise**
   - ADC readings from an unused or floating pin (or the built-in light sensor / LDR if present).
   - Wi-Fi RSSI jitter if Wi-Fi is initialized (optional).
   - ESP32 hardware RNG (`esp_fill_random` or `esp_random`) mixed in as a secondary source.

3. **Mixing**
   - Accumulate samples into a buffer (e.g., 64–256 bytes).
   - Periodically or at end of session: hash the buffer with SHA-256 (mbedtls or ESP-IDF `mbedtls_sha256`).
   - Use the hash as the entropy source for BIP39.

**Target thresholds (tunable in `config.h`):**
- ≥ 90 seconds of continuous play **and** sufficient samples → offer 128-bit / 12-word seed.
- ≥ 180 seconds (or higher action count) → offer 256-bit / 24-word seed.
- Below threshold → show score only, no seed.

Display live or end-of-run entropy estimate (approximate bit count based on sample volume and estimated entropy per sample).

---

## 3. BIP39 Generation

- Embed the official English BIP39 wordlist (2048 words) as a PROGMEM / const array or load from SD if preferred.
- Implement standard BIP39:
  - Entropy → SHA-256 checksum → split into 11-bit indices → wordlist lookup.
- Support both 128-bit (12 words) and 256-bit (24 words).
- Display the phrase word-by-word or line-by-line with scroll / "Next" button (screen is only 240×320).
- Show clear labeling: "DEMO SEED — DO NOT USE FOR REAL FUNDS".

---

## 4. MicroSD Card Save

After seed generation:
- Offer option to save to SD.
- Strong warning on screen: "This writes the seed in plaintext. Confirm only if you understand the risk."
- Require deliberate confirmation (e.g., long press or specific button combo) before writing.
- Filename suggestion: `seed_YYYYMMDD_HHMMSS.txt` or `seed_<score>.txt`.
- File content: the full mnemonic phrase + timestamp + entropy bit estimate + clear warning header.
- Use standard Arduino `SD.h` or ESP-IDF SDMMC / SPI depending on the board's SD interface.
  - Many ESP32-S3 2.8" boards use 4-bit SDMMC rather than SPI. Detect or make pinout configurable.

---

## 5. Display & UI Constraints

- Resolution: 240×320 (portrait preferred for the game).
- Driver: typically ILI9341 or ST7789 (board-dependent). Use TFT_eSPI or LovyanGFX with a configurable setup.
- Fonts: small bitmap fonts (5×7 or Adafruit GFX fonts). Twelve or twenty-four words must scroll.
- Touch: resistive (XPT2046) or capacitive depending on board. Provide on-screen virtual buttons as fallback.
- Color scheme: high contrast (black background, yellow/white/orange coins and text) to match the "yellow display" aesthetic.

---

## 6. Suggested Project Structure

```
satoshi_courier/
├── platformio.ini          # or Arduino sketch
├── src/
│   ├── main.cpp
│   ├── game.cpp / game.h
│   ├── entropy.cpp / entropy.h
│   ├── bip39.cpp / bip39.h
│   ├── sd_save.cpp / sd_save.h
│   └── config.h            # thresholds, pins, flags
├── data/                   # or PROGMEM
│   └── bip39_english.txt
└── README.md
```

**Key libraries (Arduino / PlatformIO):**
- TFT_eSPI or LovyanGFX
- SD / SD_MMC
- mbedtls (usually built-in)
- Optional: LVGL if you want fancier UI (not required)

---

## 7. Tunable Constants (`config.h`)

```c
#define MIN_TIME_FOR_12WORD_MS   (90 * 1000)
#define MIN_TIME_FOR_24WORD_MS   (180 * 1000)
#define ENTROPY_BUFFER_SIZE      256
#define SHOW_LIVE_ENTROPY_ESTIMATE true
#define REQUIRE_CONFIRM_BEFORE_SD_WRITE true
```

---

## 8. Out of Scope / Explicit Non-Goals

- Real wallet functionality, key derivation (BIP32/BIP44), signing, or network connectivity.
- Claiming the entropy is cryptographically sufficient for production use.
- Complex physics or multiplayer.
- Persistent high-score leaderboards (optional simple local high score is fine).

---

## 9. Build & Test Notes for Claude Code

1. Start with a minimal sketch that initializes the display and draws a static "Satoshi's Courier" title screen.
2. Add basic sprite movement and coin collection.
3. Add the entropy sampling hook on every input.
4. Implement SHA-256 mixing and BIP39 conversion (test with known entropy vectors if possible).
5. Add the time-gate and seed display screens.
6. Add SD mount + write with confirmation dialog.
7. Make pin definitions and display driver easily configurable at the top of the file so the user can match their specific ESP32-S3 2.8" board.

**Pinout note:** Classic CYD (ESP32) and ESP32-S3 variants have different pinouts. Prefer a `#define` block or board-specific header so the user can adjust without hunting through the code.

---

## 10. Safety & Legal Reminder

Include an on-screen and file-header warning on every generated seed:

> DEMO ONLY — Entropy harvested from game play.  
> Not suitable for real Bitcoin storage.  
> Verify independently before any use.

---

*End of specification. Ready for Claude Code or any other agent to implement.*
