#pragma once
#include <stdbool.h>
#include "audio.h"

typedef enum {
    SFX_COIN = 0, SFX_COIN_HI, SFX_CRASH, SFX_SHIELD, SFX_LEVELUP,
    SFX_GAMEOVER, SFX_CLICK, SFX_SEED, SFX_WHOOSH, SFX_DENY, SFX_COUNT
} sfx_id_t;

bool sfx_init(void *i2c_bus);           // synthesises all effects, starts mixer task
void sfx_play(sfx_id_t id, float vol);
void sfx_music(bool on);
void sfx_music_intensity(int level);    // 0..3, rises with game speed
void sfx_master(float v);               // 0..1

// Discrete volume levels: 0 = MUTE, 3 = HIGH. Persisted in NVS so the board
// comes back up as quiet as you left it.
#define SFX_LEVELS 4
void        sfx_set_level(int lv);
void        sfx_cycle_down(void);       // HIGH -> MED -> LOW -> MUTE -> HIGH
int         sfx_level(void);
const char *sfx_level_name(void);
