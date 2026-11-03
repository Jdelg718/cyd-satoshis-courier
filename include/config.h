// Tunables for Satoshi's Courier. Everything a player-facing knob touches lives here.
#pragma once

// ---- entropy gates (spec §7) ----
#define MIN_TIME_FOR_12WORD_MS      (90 * 1000)
#define MIN_TIME_FOR_24WORD_MS      (180 * 1000)
#define MIN_ACTIONS_FOR_12WORD      60
#define MIN_ACTIONS_FOR_24WORD      150
#define SHOW_LIVE_ENTROPY_ESTIMATE  true
#define REQUIRE_CONFIRM_BEFORE_SD_WRITE true
#define SD_CONFIRM_HOLD_MS          2000      // deliberate long-press before writing

// ---- game feel ----
#define START_SHIELDS           3
#define PLAYER_SCREEN_Y         100           // upper third; finger drags below it
#define PLAYER_RADIUS           8
#define PLAYER_FOLLOW           0.26f         // touch-follow smoothing, 0..1
#define SHAFT_MARGIN            10

#define SPEED_START             115.0f        // px/sec of descent
#define SPEED_MAX               330.0f
#define SPEED_RAMP              5.2f          // px/sec added per second survived

#define GAP_START               112.0f
#define GAP_MIN                 60.0f
#define GAP_SHRINK              2.6f          // px narrower per barrier passed

#define BARRIER_SPACING         150.0f        // world px between barriers
#define MAX_BARRIERS            6
#define MAX_PICKUPS             36
#define MAX_PARTICLES           180
#define MAX_STARS               72

#define INVULN_MS               1200
#define COMBO_WINDOW_MS         1800
