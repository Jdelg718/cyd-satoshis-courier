#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum { EV_MOVE = 1, EV_TAP, EV_COIN, EV_HIT, EV_DODGE, EV_MENU } ev_kind_t;

bool     entropy_init(void);            // true-random floor + DRBG seeding
void     entropy_begin_run(void);
void     entropy_event(ev_kind_t kind, int32_t a, int32_t b);
void     entropy_sample_hw(void);       // floating ADC taps + MEMS mic
uint32_t entropy_actions(void);
uint32_t entropy_play_bits(void);       // gameplay-derived estimate (the game mechanic)
uint32_t entropy_run_ms(void);
bool     entropy_generate(uint8_t *out, size_t n);
const char *entropy_sources(void);
