#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "board.h"

// dout_pin is parameterised so the bring-up test can resolve the IO6/IO8
// DOUT/DIN disagreement between the vendor docs and the community BSP.
bool audio_init(void *i2c_bus, int dout_pin, int din_pin);
void audio_deinit(void);
void audio_amp(bool on);
void audio_volume(int pct);
void audio_tone(float freq_hz, int ms, float amp);   // blocking test tone
size_t audio_write(const int16_t *mono, size_t n_samples);
size_t audio_read(int16_t *mono, size_t n_samples);

// Plays `freq` out of the speaker and listens on the onboard MEMS mic,
// returning the Goertzel magnitude at that exact frequency divided by the
// magnitude measured in silence. A ratio >> 1 means the loop really closed.
float  audio_loopback_probe(float freq, int ms);
