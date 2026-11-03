#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "board.h"

// Framebuffer stores pre-byte-swapped RGB565 (ILI9341 wants big-endian),
// so RGB() does the swap at compile time and blits stay a plain memcpy.
#define RGB(r, g, b)  ((uint16_t)((((uint16_t)(((r) & 0xF8) << 8 | ((g) & 0xFC) << 3 | ((b) >> 3))) >> 8) | \
                                  (((uint16_t)(((r) & 0xF8) << 8 | ((g) & 0xFC) << 3 | ((b) >> 3))) << 8)))

#define C_BLACK     RGB(0,0,0)
#define C_WHITE     RGB(255,255,255)
#define C_ORANGE    RGB(247,147,26)   // Bitcoin orange
#define C_YELLOW    RGB(255,214,0)
#define C_RED       RGB(255,48,48)
#define C_GREEN     RGB(0,230,118)
#define C_CYAN      RGB(0,229,255)
#define C_DIM       RGB(90,90,110)

extern uint16_t *fb;   // LCD_W * LCD_H, in PSRAM

void     disp_init(void);
void     disp_flush(void);            // blocking: fine for menus
// Double-buffered path: starts the DMA and immediately swaps `fb` to the other
// buffer, so the next frame's logic runs during the 15.4 ms transfer.
void     disp_flush_async(void);
void     disp_wait_flush(void);
void     disp_backlight(bool on);
void     disp_clear(uint16_t c);
void     disp_px(int x, int y, uint16_t c);
void     disp_fill_rect(int x, int y, int w, int h, uint16_t c);
void     disp_rect(int x, int y, int w, int h, uint16_t c);
void     disp_circle(int cx, int cy, int r, uint16_t c);
void     disp_fill_circle(int cx, int cy, int r, uint16_t c);
void     disp_text(int x, int y, const char *s, uint16_t c, int scale);
void     disp_text_center(int cy, const char *s, uint16_t c, int scale);
int      disp_text_width(const char *s, int scale);

// ---- blending / effects (framebuffer is byte-swapped, handled internally) ----
uint16_t disp_lerp(uint16_t a, uint16_t b, uint8_t t);        // t: 0..255
void     disp_blend_px(int x, int y, uint16_t c, uint8_t a);
void     disp_blend_rect(int x, int y, int w, int h, uint16_t c, uint8_t a);
void     disp_add_px(int x, int y, uint8_t r, uint8_t g, uint8_t b);
void     disp_glow(int cx, int cy, int r, uint16_t c, uint8_t a);
void     disp_vgradient(int y, int h, uint16_t top, uint16_t bot);
void     disp_line(int x0, int y0, int x1, int y1, uint16_t c);
void     disp_thick_line(int x0, int y0, int x1, int y1, int w, uint16_t c);
void     disp_text_glow(int x, int y, const char *s, uint16_t c, int scale);
