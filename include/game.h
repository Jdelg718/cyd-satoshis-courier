#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t score;
    uint32_t sats;
    uint32_t depth_m;
    uint32_t run_ms;
    uint32_t actions;
    uint32_t best_combo;
} run_result_t;

void game_init(void);
void game_run(void);        // owns the main loop, never returns

// Polls the BOOT button (physical, works on every screen) and draws a brief
// volume overlay after a change. Call once per frame, before the flush.
void ui_volume_tick(void);
bool ui_volume_hit(int x, int y);   // on-screen widget bounds on the title
void ui_draw_volume_widget(float t);
