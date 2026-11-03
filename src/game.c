// Satoshi's Courier - endless vertical descent.
//
// The player falls down a neon shaft. The camera tracks the fall, so terrain
// enters from the bottom and rushes upward past a ship parked in the upper
// third. That layout is deliberate: on a 240x320 portrait panel the finger
// lives in the lower two-thirds, so keeping the ship high stops the hand
// covering the thing you are steering.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "config.h"
#include "board.h"
#include "display.h"
#include "touch.h"
#include "sfx.h"
#include "rgbled.h"
#include "entropy.h"
#include "seedui.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "game";

typedef enum { ST_TITLE, ST_PLAY, ST_DYING, ST_OVER } gstate_t;

typedef struct { float x, y, vx, vy, life, life0; uint16_t col; uint8_t sz; } particle_t;
typedef struct { float wy, gap_x, gap_w, drift; bool active, scored; } barrier_t;
typedef struct { float wx, wy, spin; bool active; uint8_t kind; } pickup_t;   // 0=sat 1=shield
typedef struct { float x, y, z; } star_t;

static gstate_t  st;
static float     cam_y;               // world y at top of screen
static float     player_wy;           // player world y
static float     player_x, player_vx, target_x;
static float     speed, gap_w;
static int       shields, combo, best_combo;
static uint32_t  score, sats, barriers_passed;
static int64_t   invuln_until, last_coin_us, state_since;
static float     shake;
static float     next_barrier_wy;

static barrier_t barriers[MAX_BARRIERS];
static pickup_t  pickups[MAX_PICKUPS];
static particle_t parts[MAX_PARTICLES];
static star_t    stars[MAX_STARS];

static uint32_t  hiscore = 0;
static bool      touch_was_down = false;
static float     last_target_x = 0;
static bool      boot_prev = false;
static int64_t   vol_shown_until = 0;

static inline float frand(void) { return (float)(esp_random() & 0xFFFF) / 65535.0f; }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---------------- volume ----------------
// The BOOT button is the only physical input on this board, so it owns the
// volume. Cycling *downward* means one press from full is already quieter and
// three presses reach silence - the right direction when someone needs it off.
void ui_volume_tick(void)
{
    bool bp = board_boot_pressed();
    if (bp && !boot_prev) {
        sfx_cycle_down();
        vol_shown_until = esp_timer_get_time() + 1400000;
    }
    boot_prev = bp;

    if (esp_timer_get_time() < vol_shown_until) {
        const char *name = sfx_level_name();
        int lv = sfx_level();
        int w = 128, h = 52, x = (LCD_W - w) / 2, y = (LCD_H - h) / 2;
        disp_blend_rect(x, y, w, h, C_BLACK, 215);
        disp_rect(x, y, w, h, lv ? C_CYAN : C_RED);
        disp_text_center(y + 9, lv ? "VOLUME" : "MUTED", lv ? C_DIM : C_RED, 1);
        int tw = disp_text_width(name, 2);
        disp_text(x + (w - tw) / 2, y + 22, name, lv ? C_WHITE : C_RED, 2);
        for (int i = 0; i < SFX_LEVELS - 1; i++) {
            int bx = x + 24 + i * 28;
            bool on = i < lv;
            disp_fill_rect(bx, y + 42, 22, 5, on ? C_CYAN : RGB(40, 44, 56));
        }
    }
}

bool ui_volume_hit(int x, int y) { return x < 76 && y < 30; }

void ui_draw_volume_widget(float t)
{
    int lv = sfx_level();
    uint16_t c = lv ? C_CYAN : C_RED;
    disp_blend_rect(6, 6, 64, 20, C_BLACK, 130);
    disp_rect(6, 6, 64, 20, disp_lerp(C_BLACK, c, 150));
    // speaker glyph
    disp_fill_rect(13, 13, 3, 6, c);
    for (int i = 0; i < 5; i++) disp_fill_rect(16 + i, 11 + (4 - i) / 2, 1, 10 - (4 - i), c);
    if (lv == 0) {
        disp_line(23, 11, 31, 21, C_RED);
        disp_line(31, 11, 23, 21, C_RED);
    } else {
        for (int i = 0; i < lv; i++) disp_fill_rect(25 + i * 4, 18 - i * 3, 2, 3 + i * 3, c);
    }
    disp_text(40, 12, sfx_level_name(), c, 1);
}

// ---------------- particles ----------------
static void spawn_part(float x, float y, float vx, float vy, float life, uint16_t col, int sz)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (parts[i].life > 0.0f) continue;
        parts[i] = (particle_t){ x, y, vx, vy, life, life, col, (uint8_t)sz };
        return;
    }
}

static void burst(float x, float y, int n, uint16_t col, float spd)
{
    for (int i = 0; i < n; i++) {
        float a = frand() * 6.2831853f;
        float s = spd * (0.35f + frand() * 0.65f);
        spawn_part(x, y, cosf(a) * s, sinf(a) * s, 0.30f + frand() * 0.45f, col,
                   1 + (int)(frand() * 2.5f));
    }
}

// ---------------- world ----------------
static void spawn_barrier(float wy)
{
    for (int i = 0; i < MAX_BARRIERS; i++) {
        if (barriers[i].active) continue;
        float w = gap_w;
        float minx = SHAFT_MARGIN + w * 0.5f + 4;
        float maxx = LCD_W - SHAFT_MARGIN - w * 0.5f - 4;
        barriers[i] = (barrier_t){
            .wy = wy,
            .gap_x = minx + frand() * (maxx - minx),
            .gap_w = w,
            .drift = (barriers_passed > 6 && frand() < 0.35f) ? (frand() < 0.5f ? -22.0f : 22.0f) : 0.0f,
            .active = true, .scored = false,
        };

        // Sats strung through and around the gap - the reward for threading it well.
        int n = 3 + (int)(frand() * 3);
        for (int k = 0; k < n; k++) {
            for (int p = 0; p < MAX_PICKUPS; p++) {
                if (pickups[p].active) continue;
                pickups[p] = (pickup_t){
                    .wx = barriers[i].gap_x + (frand() - 0.5f) * (w * 0.55f),
                    .wy = wy - 48.0f - k * 17.0f,
                    .spin = frand() * 6.28f, .active = true, .kind = 0,
                };
                break;
            }
        }
        // Occasional shield, always off the racing line so it costs a detour.
        if (barriers_passed > 3 && frand() < 0.16f) {
            for (int p = 0; p < MAX_PICKUPS; p++) {
                if (pickups[p].active) continue;
                float side = frand() < 0.5f ? -1.0f : 1.0f;
                pickups[p] = (pickup_t){
                    .wx = clampf(barriers[i].gap_x + side * (w * 0.5f + 26.0f),
                                 SHAFT_MARGIN + 12, LCD_W - SHAFT_MARGIN - 12),
                    .wy = wy + 60.0f, .spin = 0, .active = true, .kind = 1,
                };
                break;
            }
        }
        return;
    }
}

static void reset_run(void)
{
    memset(barriers, 0, sizeof(barriers));
    memset(pickups, 0, sizeof(pickups));
    memset(parts, 0, sizeof(parts));
    cam_y = 0;
    player_wy = PLAYER_SCREEN_Y;
    player_x = target_x = last_target_x = LCD_W / 2.0f;
    player_vx = 0;
    speed = SPEED_START;
    gap_w = GAP_START;
    shields = START_SHIELDS;
    combo = best_combo = 0;
    score = sats = barriers_passed = 0;
    shake = 0;
    invuln_until = 0;
    next_barrier_wy = PLAYER_SCREEN_Y + 320.0f;
    for (int i = 0; i < MAX_STARS; i++)
        stars[i] = (star_t){ frand() * LCD_W, frand() * LCD_H, 0.25f + frand() * 1.0f };
    entropy_begin_run();
}

// ---------------- drawing ----------------
static void draw_background(float sy)
{
    disp_vgradient(0, LCD_H, RGB(10, 8, 26), RGB(2, 2, 8));

    // Parallax dust: nearer motes stream faster, which is most of the speed read.
    for (int i = 0; i < MAX_STARS; i++) {
        star_t *s = &stars[i];
        s->y -= speed * s->z * 0.016f;
        if (s->y < 0) { s->y += LCD_H; s->x = frand() * LCD_W; }
        uint8_t b = (uint8_t)(40 + s->z * 150);
        int yy = (int)(s->y + sy);
        if (s->z > 0.8f) {
            disp_add_px((int)s->x, yy, b >> 3, b >> 3, b >> 2);
            disp_add_px((int)s->x, yy + 1, b >> 4, b >> 4, b >> 3);
        } else {
            disp_add_px((int)s->x, yy, b >> 4, b >> 4, b >> 3);
        }
    }

    // Shaft walls with scrolling rungs - a cheap, strong sense of descent.
    float phase = fmodf(cam_y, 28.0f);
    for (float y = -phase; y < LCD_H; y += 28.0f) {
        int yy = (int)(y + sy);
        disp_blend_rect(0, yy, SHAFT_MARGIN - 2, 3, RGB(120, 60, 180), 150);
        disp_blend_rect(LCD_W - SHAFT_MARGIN + 2, yy, SHAFT_MARGIN - 2, 3, RGB(120, 60, 180), 150);
    }
    disp_blend_rect(0, 0, 2, LCD_H, RGB(150, 80, 255), 190);
    disp_blend_rect(LCD_W - 2, 0, 2, LCD_H, RGB(150, 80, 255), 190);
}

static void fill_ellipse(int cx, int cy, int rx, int ry, uint16_t c)
{
    if (rx < 1) rx = 1;
    for (int dy = -ry; dy <= ry; dy++) {
        float t = (float)dy / ry;
        int w = (int)(rx * sqrtf(1.0f - t * t) + 0.5f);
        disp_fill_rect(cx - w, cy + dy, w * 2 + 1, 1, c);
    }
}

static void draw_coin(int x, int y, float spin)
{
    int rx = (int)(6.0f * fabsf(cosf(spin)) + 1.0f);
    disp_glow(x, y, 11, C_ORANGE, 70);
    fill_ellipse(x, y, rx, 6, C_ORANGE);
    if (rx > 3) {
        fill_ellipse(x, y, rx - 2, 4, RGB(255, 205, 90));
        disp_fill_rect(x - 1, y - 3, 2, 6, RGB(120, 60, 0));   // the "S" bar, abstracted
    }
}

static void draw_shield_pickup(int x, int y, float t)
{
    uint8_t a = (uint8_t)(150 + 100 * sinf(t * 6.0f));
    disp_glow(x, y, 13, C_CYAN, 80);
    disp_circle(x, y, 8, C_CYAN);
    disp_circle(x, y, 6, disp_lerp(C_BLACK, C_CYAN, a));
    disp_fill_rect(x - 1, y - 4, 3, 8, C_CYAN);
    disp_fill_rect(x - 4, y - 1, 8, 3, C_CYAN);
}

static void draw_ship(float x, float y, float vx, bool flashing, float t)
{
    if (flashing && ((int)(t * 18.0f) & 1)) return;

    int cx = (int)x, cy = (int)y;
    float bank = clampf(vx * 0.045f, -6.0f, 6.0f);

    // Thruster plume above (we are falling, so the exhaust streams up).
    float f = 9.0f + frand() * 7.0f;
    for (int i = 0; i < (int)f; i++) {
        int w = (int)(5 - i * 0.42f);
        if (w < 1) break;
        uint16_t c = disp_lerp(C_ORANGE, RGB(255, 60, 0), (uint8_t)(i * 255 / (int)f));
        disp_fill_rect(cx - w / 2, cy - 10 - i, w, 1, c);
    }
    disp_glow(cx, cy - 12, 10, RGB(255, 120, 0), 90);

    // Hull: a dart pointing down into the shaft.
    uint16_t body = RGB(255, 255, 255), edge = C_ORANGE;
    for (int i = 0; i <= 16; i++) {
        float tt = i / 16.0f;
        int w = (int)((1.0f - tt) * 9.0f);
        int yy = cy - 8 + i;
        int xx = cx + (int)(bank * tt);
        disp_fill_rect(xx - w, yy, w * 2 + 1, 1, i < 3 ? edge : body);
    }
    disp_fill_rect(cx - 2 + (int)bank, cy - 2, 4, 6, C_ORANGE);
    disp_glow(cx, cy, 13, C_ORANGE, 55);
}

static void draw_barrier(const barrier_t *b, float sy)
{
    int y = (int)(b->wy - cam_y + sy);
    if (y < -14 || y > LCD_H + 14) return;

    int gl = (int)(b->gap_x - b->gap_w * 0.5f);
    int gr = (int)(b->gap_x + b->gap_w * 0.5f);
    uint16_t core = b->drift != 0.0f ? RGB(255, 60, 120) : RGB(120, 70, 255);
    uint16_t hot  = b->drift != 0.0f ? RGB(255, 170, 200) : RGB(200, 180, 255);

    disp_fill_rect(SHAFT_MARGIN, y - 5, gl - SHAFT_MARGIN, 11, core);
    disp_fill_rect(gr, y - 5, LCD_W - SHAFT_MARGIN - gr, 11, core);
    disp_fill_rect(SHAFT_MARGIN, y - 5, gl - SHAFT_MARGIN, 2, hot);
    disp_fill_rect(gr, y - 5, LCD_W - SHAFT_MARGIN - gr, 2, hot);

    // Gap mouth: bright posts so the safe lane reads instantly.
    disp_fill_rect(gl - 3, y - 7, 3, 15, hot);
    disp_fill_rect(gr, y - 7, 3, 15, hot);
    disp_glow(gl, y, 13, hot, 95);
    disp_glow(gr, y, 13, hot, 95);
}

static void draw_hud(void)
{
    char buf[40];
    disp_blend_rect(0, 0, LCD_W, 30, RGB(0, 0, 0), 120);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)score);
    disp_text_glow(6, 5, buf, C_WHITE, 2);

    snprintf(buf, sizeof(buf), "%lu SAT", (unsigned long)sats);
    disp_text(6, 20, buf, C_ORANGE, 1);

    snprintf(buf, sizeof(buf), "%lum", (unsigned long)(cam_y / 10.0f));
    disp_text(LCD_W - disp_text_width(buf, 1) - 6, 20, buf, C_DIM, 1);

    for (int i = 0; i < START_SHIELDS; i++) {
        int x = LCD_W - 12 - i * 11;
        if (i < shields) { disp_fill_circle(x, 10, 4, C_CYAN); disp_glow(x, 10, 7, C_CYAN, 70); }
        else disp_circle(x, 10, 4, RGB(50, 60, 70));
    }

    if (combo > 1) {
        snprintf(buf, sizeof(buf), "x%d", combo);
        disp_text_glow(LCD_W / 2 - 8, 20, buf, C_YELLOW, 1);
    }

#if SHOW_LIVE_ENTROPY_ESTIMATE
    // Live entropy meter - the whole point of the machine, so it stays on screen.
    uint32_t bits = entropy_play_bits();
    uint32_t ms   = entropy_run_ms();
    int w12 = (int)(((float)ms / MIN_TIME_FOR_12WORD_MS) * 100.0f);
    if (w12 > 100) w12 = 100;
    disp_fill_rect(6, LCD_H - 9, 100, 4, RGB(26, 26, 38));
    uint16_t bc = (ms >= MIN_TIME_FOR_24WORD_MS) ? C_GREEN
                : (ms >= MIN_TIME_FOR_12WORD_MS) ? C_YELLOW : C_ORANGE;
    disp_fill_rect(6, LCD_H - 9, w12, 4, bc);
    snprintf(buf, sizeof(buf), "%lu bits", (unsigned long)bits);
    disp_text(112, LCD_H - 10, buf, C_DIM, 1);
#endif
}

// ---------------- update ----------------
#ifdef STRESS_AUTOPLAY
static bool read_input(float *tx)
{
    float t = esp_timer_get_time() / 1000000.0f;
    *tx = LCD_W / 2.0f + sinf(t * 1.7f) * 80.0f + sinf(t * 4.3f) * 25.0f;
    return true;
}
static int last_touch_y = 0;
#else
static int last_touch_y = 0;

static bool read_input(float *tx)
{
    touch_pt_t p;
    if (touch_read(&p) && p.pressed) {
        last_touch_y = p.y;
        *tx = clampf((float)p.x, SHAFT_MARGIN + PLAYER_RADIUS, LCD_W - SHAFT_MARGIN - PLAYER_RADIUS);
        return true;
    }
    return false;
}
#endif

static void hit_player(void)
{
    if (esp_timer_get_time() < invuln_until) return;
#ifdef STRESS_AUTOPLAY
    shields = START_SHIELDS;
#endif
    shields--;
    combo = 0;
    shake = 9.0f;
    burst(player_x, PLAYER_SCREEN_Y, 26, C_RED, 150.0f);
    sfx_play(SFX_CRASH, 0.95f);
    rgbled_set(120, 0, 0);
    entropy_event(EV_HIT, (int32_t)player_x, (int32_t)cam_y);
    invuln_until = esp_timer_get_time() + INVULN_MS * 1000;
    if (shields <= 0) {
        st = ST_DYING;
        state_since = esp_timer_get_time();
        sfx_music(false);
        sfx_play(SFX_GAMEOVER, 1.0f);
        burst(player_x, PLAYER_SCREEN_Y, 40, C_ORANGE, 220.0f);
    }
}

static void update_play(float dt)
{
    int64_t now = esp_timer_get_time();

    // --- input ---
    bool down = read_input(&target_x);
    if (down) {
        if (fabsf(target_x - last_target_x) > 6.0f) {
            entropy_event(EV_MOVE, (int32_t)target_x, (int32_t)(now & 0xFFFF));
            last_target_x = target_x;
        }
        if (!touch_was_down) { entropy_event(EV_TAP, (int32_t)target_x, 0); sfx_play(SFX_WHOOSH, 0.25f); }
    }
    touch_was_down = down;

    float px = player_x;
    player_x += (target_x - player_x) * PLAYER_FOLLOW;
    player_vx = (player_x - px) / dt;
    player_x = clampf(player_x, SHAFT_MARGIN + PLAYER_RADIUS, LCD_W - SHAFT_MARGIN - PLAYER_RADIUS);

    // --- descent ---
    speed = clampf(speed + SPEED_RAMP * dt, SPEED_START, SPEED_MAX);
    cam_y += speed * dt;
    player_wy = cam_y + PLAYER_SCREEN_Y;
    score += (uint32_t)(speed * dt * 0.06f);

    sfx_music_intensity(speed > 260.0f ? 3 : speed > 195.0f ? 2 : speed > 150.0f ? 1 : 1);

    // thruster trail
    if ((esp_random() & 3) == 0)
        spawn_part(player_x + (frand() - 0.5f) * 5, PLAYER_SCREEN_Y - 11,
                   (frand() - 0.5f) * 20, -speed * 0.35f - frand() * 40,
                   0.25f + frand() * 0.2f, C_ORANGE, 1);

    // --- spawn ---
    while (next_barrier_wy < cam_y + LCD_H + 60.0f) {
        spawn_barrier(next_barrier_wy);
        next_barrier_wy += BARRIER_SPACING;
    }

    // --- barriers ---
    for (int i = 0; i < MAX_BARRIERS; i++) {
        barrier_t *b = &barriers[i];
        if (!b->active) continue;
        if (b->drift != 0.0f) {
            b->gap_x += b->drift * dt;
            float lo = SHAFT_MARGIN + b->gap_w * 0.5f + 4, hi = LCD_W - SHAFT_MARGIN - b->gap_w * 0.5f - 4;
            if (b->gap_x < lo) { b->gap_x = lo; b->drift = -b->drift; }
            if (b->gap_x > hi) { b->gap_x = hi; b->drift = -b->drift; }
        }
        if (b->wy < cam_y - 20.0f) { b->active = false; continue; }

        // collision: player band vs the solid parts of the barrier
        float dy = fabsf(b->wy - player_wy);
        if (dy < 5.0f + PLAYER_RADIUS) {
            float gl = b->gap_x - b->gap_w * 0.5f, gr = b->gap_x + b->gap_w * 0.5f;
            if (player_x - PLAYER_RADIUS < gl || player_x + PLAYER_RADIUS > gr) hit_player();
        }
        if (!b->scored && b->wy < player_wy) {
            b->scored = true;
            barriers_passed++;
            gap_w = clampf(GAP_START - barriers_passed * GAP_SHRINK, GAP_MIN, GAP_START);
            score += 25;
            entropy_event(EV_DODGE, (int32_t)(player_x - b->gap_x), (int32_t)barriers_passed);
            if (barriers_passed % 10 == 0) sfx_play(SFX_LEVELUP, 0.6f);
        }
    }

    // --- pickups ---
    for (int i = 0; i < MAX_PICKUPS; i++) {
        pickup_t *k = &pickups[i];
        if (!k->active) continue;
        k->spin += dt * 5.5f;
        if (k->wy < cam_y - 20.0f) { k->active = false; continue; }

        float dx = k->wx - player_x, dy = k->wy - player_wy;
        if (dx * dx + dy * dy < (PLAYER_RADIUS + 9) * (PLAYER_RADIUS + 9)) {
            k->active = false;
            if (k->kind == 0) {
                if (now - last_coin_us < COMBO_WINDOW_MS * 1000) combo++; else combo = 1;
                if (combo > best_combo) best_combo = combo;
                last_coin_us = now;
                sats++;
                score += 10 * (combo > 8 ? 8 : combo);
                sfx_play(combo > 3 ? SFX_COIN_HI : SFX_COIN, 0.7f);
                burst(k->wx, k->wy - cam_y, 8, C_YELLOW, 90.0f);
                rgbled_set(80, 45, 0);
                entropy_event(EV_COIN, (int32_t)k->wx, combo);
            } else {
                if (shields < START_SHIELDS) shields++;
                sfx_play(SFX_SHIELD, 0.8f);
                burst(k->wx, k->wy - cam_y, 14, C_CYAN, 110.0f);
                rgbled_set(0, 90, 90);
                entropy_event(EV_COIN, (int32_t)k->wx, 100);
            }
        }
    }

    if (shake > 0.0f) shake = fmaxf(0.0f, shake - dt * 26.0f);
}

static void update_particles(float dt)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particle_t *p = &parts[i];
        if (p->life <= 0.0f) continue;
        p->life -= dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->vy += 120.0f * dt;
    }
}

static void draw_particles(float sy)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particle_t *p = &parts[i];
        if (p->life <= 0.0f) continue;
        uint8_t a = (uint8_t)(255.0f * (p->life / p->life0));
        int x = (int)p->x, y = (int)(p->y + sy);
        if (p->sz <= 1) disp_blend_px(x, y, p->col, a);
        else disp_blend_rect(x - p->sz / 2, y - p->sz / 2, p->sz, p->sz, p->col, a);
    }
}

static void draw_world(float sy, bool with_ship, float t)
{
    draw_background(sy);
    for (int i = 0; i < MAX_BARRIERS; i++)
        if (barriers[i].active) draw_barrier(&barriers[i], sy);
    for (int i = 0; i < MAX_PICKUPS; i++) {
        pickup_t *k = &pickups[i];
        if (!k->active) continue;
        int y = (int)(k->wy - cam_y + sy);
        if (y < -16 || y > LCD_H + 16) continue;
        if (k->kind == 0) draw_coin((int)k->wx, y, k->spin);
        else draw_shield_pickup((int)k->wx, y, t);
    }
    draw_particles(sy);
    if (with_ship)
        draw_ship(player_x, PLAYER_SCREEN_Y, player_vx,
                  esp_timer_get_time() < invuln_until, t);
}

// ---------------- screens ----------------
static void draw_title(float t)
{
    cam_y += 40.0f * 0.016f;
    draw_background(0);

    float bob = sinf(t * 1.6f) * 5.0f;
    disp_glow(LCD_W / 2, 92 + (int)bob, 34, C_ORANGE, 70);
    disp_fill_circle(LCD_W / 2, 92 + (int)bob, 24, C_ORANGE);
    disp_fill_circle(LCD_W / 2, 92 + (int)bob, 20, RGB(255, 190, 70));
    disp_text_glow(LCD_W / 2 - 5, 84 + (int)bob, "S", RGB(120, 60, 0), 3);

    disp_text_center(146, "SATOSHIS", C_WHITE, 3);
    disp_text_center(176, "COURIER", C_ORANGE, 3);
    disp_text_center(206, "harvest entropy by flying badly", C_DIM, 1);

    uint8_t a = (uint8_t)(150 + 105 * sinf(t * 3.4f));
    disp_text_center(246, "TAP TO DESCEND", disp_lerp(C_BLACK, C_CYAN, a), 2);

    char buf[40];
    if (hiscore) {
        snprintf(buf, sizeof(buf), "BEST %lu", (unsigned long)hiscore);
        disp_text_center(282, buf, C_YELLOW, 1);
    }
    disp_text_center(300, "demo seeds only - not for real funds", RGB(120, 60, 60), 1);
    ui_draw_volume_widget(t);
    disp_text(6, 30, "BOOT btn = volume", RGB(70, 80, 100), 1);
}

static void draw_gameover(float t)
{
    draw_world(0, false, t);
    disp_blend_rect(0, 0, LCD_W, LCD_H, C_BLACK, 170);
    disp_blend_rect(0, 84, LCD_W, 124, C_BLACK, 150);
    disp_text_center(96, "RUN ENDED", C_RED, 3);

    char buf[48];
    snprintf(buf, sizeof(buf), "SCORE %lu", (unsigned long)score);
    disp_text_center(142, buf, C_WHITE, 2);
    snprintf(buf, sizeof(buf), "%lu sats  %lum  x%d best",
             (unsigned long)sats, (unsigned long)(cam_y / 10.0f), best_combo);
    disp_text_center(168, buf, C_ORANGE, 1);

    uint32_t ms = entropy_run_ms();
    snprintf(buf, sizeof(buf), "flight time %lu.%lus", (unsigned long)(ms / 1000), (unsigned long)((ms % 1000) / 100));
    disp_text_center(192, buf, C_DIM, 1);

    // The joke, stated plainly: survival time is what buys you a seed.
    const char *verdict;
    uint16_t vc;
    if (ms >= MIN_TIME_FOR_24WORD_MS && entropy_actions() >= MIN_ACTIONS_FOR_24WORD) {
        verdict = "24-WORD SEED UNLOCKED"; vc = C_GREEN;
    } else if (ms >= MIN_TIME_FOR_12WORD_MS && entropy_actions() >= MIN_ACTIONS_FOR_12WORD) {
        verdict = "12-WORD SEED UNLOCKED"; vc = C_YELLOW;
    } else {
        verdict = "TOO SHORT FOR A SEED"; vc = C_RED;
    }
    int vw = disp_text_width(verdict, 1);
    disp_fill_rect((LCD_W - vw) / 2 - 8, 218, vw + 16, 16, C_BLACK);
    disp_rect((LCD_W - vw) / 2 - 8, 218, vw + 16, 16, disp_lerp(C_BLACK, vc, 90));
    disp_text_center(224, verdict, vc, 1);

    uint8_t a = (uint8_t)(140 + 110 * sinf(t * 3.4f));
    disp_text_center(262, "TAP TO CONTINUE", disp_lerp(C_BLACK, C_WHITE, a), 1);
}

// ---------------- main loop ----------------
void game_init(void)
{
#ifdef STRESS_AUTOPLAY
    st = ST_PLAY;
#else
    st = ST_TITLE;
#endif
    state_since = esp_timer_get_time();
    reset_run();
    sfx_music(true);
    sfx_music_intensity(1);
    rgbled_set(20, 10, 0);
}

void game_run(void)
{
    int64_t prev = esp_timer_get_time();
    int64_t prev_report_t = prev;
    int frame = 0;

    while (1) {
        int64_t now = esp_timer_get_time();
        float dt = (now - prev) / 1000000.0f;
        prev = now;
        if (dt > 0.05f) dt = 0.05f;          // never let a stall catapult the player
        float t = now / 1000000.0f;

        if ((frame++ & 15) == 0) entropy_sample_hw();

        float sy = 0.0f;
        if (shake > 0.1f) sy = (frand() - 0.5f) * shake;

        switch (st) {
        case ST_TITLE: {
            draw_title(t);
            float tx;
            bool down = read_input(&tx);
            if (down && !touch_was_down && ui_volume_hit((int)tx, last_touch_y)) {
                sfx_cycle_down();
                vol_shown_until = now + 1400000;
                touch_was_down = down;
                break;
            }
            if (down && !touch_was_down) {
                sfx_play(SFX_CLICK, 0.8f);
                entropy_event(EV_MENU, (int32_t)tx, 0);
                reset_run();
                sfx_music(true);
                st = ST_PLAY;
                state_since = now;
            }
            touch_was_down = down;
            break;
        }

        case ST_PLAY:
            update_play(dt);
            update_particles(dt);
            draw_world(sy, true, t);
            draw_hud();
            if (shields > 0 && (frame & 31) == 0) {
                int g = 20 + (int)(speed / SPEED_MAX * 60);
                rgbled_set(4, g, 8);
            }
            break;

        case ST_DYING:
            update_particles(dt);
            cam_y += speed * dt * 0.25f;
            speed *= 0.965f;
            draw_world(sy, false, t);
            draw_hud();
            if (now - state_since > 1400000) {
                st = ST_OVER;
                state_since = now;
                if (score > hiscore) hiscore = score;
                touch_was_down = true;          // swallow the touch that killed the run
            }
            break;

        case ST_OVER: {
            update_particles(dt);
            draw_gameover(t);
            float tx;
            bool down = read_input(&tx);
            if (down && !touch_was_down && (now - state_since) > 600000) {
                sfx_play(SFX_CLICK, 0.8f);
                run_result_t r = {
                    .score = score, .sats = sats,
                    .depth_m = (uint32_t)(cam_y / 10.0f),
                    .run_ms = entropy_run_ms(),
                    .actions = entropy_actions(),
                    .best_combo = (uint32_t)best_combo,
                };
                seedui_run(&r);                 // takes over the screen, then returns
                game_init();
                prev = esp_timer_get_time();
                continue;
            }
            touch_was_down = down;
            break;
        }
        }

        ui_volume_tick();

        int64_t logic_end = esp_timer_get_time();
        disp_flush_async();

        // --- frame budget telemetry ---
        // `logic` is CPU time spent building the frame; `period` is the real
        // frame interval. While logic < the 15.4 ms DMA, the panel bandwidth is
        // the limit and we have headroom to spare.
        static int64_t acc_logic = 0, acc_period = 0, last_report = 0;
        static int nframes = 0;
        acc_logic += logic_end - now;
        acc_period += now - prev_report_t;
        prev_report_t = now;
        if (++nframes >= 120) {
            ESP_LOGI(TAG, "fps %.1f  logic %.1f ms  budget %.0f%%  st=%d",
                     1000000.0f * nframes / (float)acc_period,
                     acc_logic / 1000.0f / nframes,
                     100.0f * acc_logic / (float)acc_period, (int)st);
            acc_logic = acc_period = 0; nframes = 0;
            (void)last_report;
        }

        // Pace to ~60 Hz. The async flush already blocks on the previous DMA,
        // so this only matters when a frame finished unusually fast.
        int64_t spent = esp_timer_get_time() - now;
        if (spent < 15000) vTaskDelay(pdMS_TO_TICKS(1));
    }
}
