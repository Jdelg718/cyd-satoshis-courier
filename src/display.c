#include <string.h>
#include <stdlib.h>
#include "display.h"
#include <math.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "disp";
uint16_t *fb = NULL;                 // current draw target
static uint16_t *fb_a = NULL, *fb_b = NULL;
static esp_lcd_panel_io_handle_t io = NULL;
static SemaphoreHandle_t flush_sem = NULL;
static volatile bool flush_pending = false;

static bool IRAM_ATTR on_flush_done(esp_lcd_panel_io_handle_t p,
                                    esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(flush_sem, &hp);
    return hp == pdTRUE;
}

static const uint8_t font5x7[] = {
0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x5F,0x00,0x00, 0x00,0x07,0x00,0x07,0x00,
0x14,0x7F,0x14,0x7F,0x14, 0x24,0x2A,0x7F,0x2A,0x12, 0x23,0x13,0x08,0x64,0x62,
0x36,0x49,0x55,0x22,0x50, 0x00,0x05,0x03,0x00,0x00, 0x00,0x1C,0x22,0x41,0x00,
0x00,0x41,0x22,0x1C,0x00, 0x14,0x08,0x3E,0x08,0x14, 0x08,0x08,0x3E,0x08,0x08,
0x00,0x50,0x30,0x00,0x00, 0x08,0x08,0x08,0x08,0x08, 0x00,0x60,0x60,0x00,0x00,
0x20,0x10,0x08,0x04,0x02, 0x3E,0x51,0x49,0x45,0x3E, 0x00,0x42,0x7F,0x40,0x00,
0x42,0x61,0x51,0x49,0x46, 0x21,0x41,0x45,0x4B,0x31, 0x18,0x14,0x12,0x7F,0x10,
0x27,0x45,0x45,0x45,0x39, 0x3C,0x4A,0x49,0x49,0x30, 0x01,0x71,0x09,0x05,0x03,
0x36,0x49,0x49,0x49,0x36, 0x06,0x49,0x49,0x29,0x1E, 0x00,0x36,0x36,0x00,0x00,
0x00,0x56,0x36,0x00,0x00, 0x00,0x08,0x14,0x22,0x41, 0x14,0x14,0x14,0x14,0x14,
0x41,0x22,0x14,0x08,0x00, 0x02,0x01,0x51,0x09,0x06, 0x32,0x49,0x79,0x41,0x3E,
0x7E,0x11,0x11,0x11,0x7E, 0x7F,0x49,0x49,0x49,0x36, 0x3E,0x41,0x41,0x41,0x22,
0x7F,0x41,0x41,0x22,0x1C, 0x7F,0x49,0x49,0x49,0x41, 0x7F,0x09,0x09,0x01,0x01,
0x3E,0x41,0x41,0x51,0x32, 0x7F,0x08,0x08,0x08,0x7F, 0x00,0x41,0x7F,0x41,0x00,
0x20,0x40,0x41,0x3F,0x01, 0x7F,0x08,0x14,0x22,0x41, 0x7F,0x40,0x40,0x40,0x40,
0x7F,0x02,0x04,0x02,0x7F, 0x7F,0x04,0x08,0x10,0x7F, 0x3E,0x41,0x41,0x41,0x3E,
0x7F,0x09,0x09,0x09,0x06, 0x3E,0x41,0x51,0x21,0x5E, 0x7F,0x09,0x19,0x29,0x46,
0x46,0x49,0x49,0x49,0x31, 0x01,0x01,0x7F,0x01,0x01, 0x3F,0x40,0x40,0x40,0x3F,
0x1F,0x20,0x40,0x20,0x1F, 0x7F,0x20,0x18,0x20,0x7F, 0x63,0x14,0x08,0x14,0x63,
0x03,0x04,0x78,0x04,0x03, 0x61,0x51,0x49,0x45,0x43, 0x00,0x00,0x7F,0x41,0x41,
0x02,0x04,0x08,0x10,0x20, 0x41,0x41,0x7F,0x00,0x00, 0x04,0x02,0x01,0x02,0x04,
0x40,0x40,0x40,0x40,0x40, 0x00,0x01,0x02,0x04,0x00, 0x20,0x54,0x54,0x54,0x78,
0x7F,0x48,0x44,0x44,0x38, 0x38,0x44,0x44,0x44,0x20, 0x38,0x44,0x44,0x48,0x7F,
0x38,0x54,0x54,0x54,0x18, 0x08,0x7E,0x09,0x01,0x02, 0x0C,0x52,0x52,0x52,0x3E,
0x7F,0x08,0x04,0x04,0x78, 0x00,0x44,0x7D,0x40,0x00, 0x20,0x40,0x44,0x3D,0x00,
0x7F,0x10,0x28,0x44,0x00, 0x00,0x41,0x7F,0x40,0x00, 0x7C,0x04,0x18,0x04,0x78,
0x7C,0x08,0x04,0x04,0x78, 0x38,0x44,0x44,0x44,0x38, 0x7C,0x14,0x14,0x14,0x08,
0x08,0x14,0x14,0x18,0x7C, 0x7C,0x08,0x04,0x04,0x08, 0x48,0x54,0x54,0x54,0x20,
0x04,0x3F,0x44,0x40,0x20, 0x3C,0x40,0x40,0x20,0x7C, 0x1C,0x20,0x40,0x20,0x1C,
0x3C,0x40,0x30,0x40,0x3C, 0x44,0x28,0x10,0x28,0x44, 0x0C,0x50,0x50,0x50,0x3C,
0x44,0x64,0x54,0x4C,0x44,
};
#define FONT_FIRST 32
#define FONT_LAST  122

// ---- ILI9341V init sequence (reset is tied to chip EN, so none issued) ----
typedef struct { uint8_t cmd; uint8_t data[16]; uint8_t len; uint8_t delay_ms; } ili_cmd_t;
static const ili_cmd_t ili_init[] = {
    {0x01, {0}, 0, 150},                                     // SWRESET
    {0xCF, {0x00, 0xC1, 0x30}, 3, 0},                        // Power control B
    {0xED, {0x64, 0x03, 0x12, 0x81}, 4, 0},                  // Power on seq
    {0xE8, {0x85, 0x00, 0x78}, 3, 0},                        // Driver timing A
    {0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02}, 5, 0},            // Power control A
    {0xF7, {0x20}, 1, 0},                                    // Pump ratio
    {0xEA, {0x00, 0x00}, 2, 0},                              // Driver timing B
    {0xC0, {0x23}, 1, 0},                                    // PWCTR1
    {0xC1, {0x10}, 1, 0},                                    // PWCTR2
    {0xC5, {0x3E, 0x28}, 2, 0},                              // VMCTR1
    {0xC7, {0x86}, 1, 0},                                    // VMCTR2
    {0x36, {0x48}, 1, 0},                                    // MADCTL: portrait, BGR
    {0x3A, {0x55}, 1, 0},                                    // PIXFMT: 16bpp
    {0xB1, {0x00, 0x13}, 2, 0},                              // FRMCTR1 ~load max fps
    {0xB6, {0x08, 0x82, 0x27}, 3, 0},                        // DFUNCTR
    {0xF2, {0x00}, 1, 0},                                    // 3Gamma off
    {0x26, {0x01}, 1, 0},                                    // Gamma curve 1
    {0xE0, {0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,0x37,0x07,0x10,0x03,0x0E,0x09,0x00}, 15, 0},
    {0xE1, {0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F}, 15, 0},
    {0x21, {0}, 0, 0},                                       // INVON (IPS panel)
    {0x11, {0}, 0, 150},                                     // SLPOUT
    {0x29, {0}, 0, 50},                                      // DISPON
};

void disp_backlight(bool on) { gpio_set_level(PIN_LCD_BL, on ? 1 : 0); }

void disp_init(void)
{
    const size_t fbsz = LCD_W * LCD_H * sizeof(uint16_t);
    fb_a = heap_caps_malloc(fbsz, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    fb_b = heap_caps_malloc(fbsz, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    assert(fb_a && fb_b && "framebuffer alloc failed");
    fb = fb_a;
    memset(fb_a, 0, fbsz); memset(fb_b, 0, fbsz);
    flush_sem = xSemaphoreCreateBinary();
    ESP_LOGI(TAG, "double framebuffer 2x%d bytes @ %p / %p", (int)fbsz, fb_a, fb_b);

    gpio_config_t bl = { .pin_bit_mask = 1ULL << PIN_LCD_BL, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&bl);
    disp_backlight(false);

    spi_bus_config_t bus = {
        .sclk_io_num     = PIN_LCD_SCK,
        .mosi_io_num     = PIN_LCD_MOSI,
        .miso_io_num     = PIN_LCD_MISO,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_W * LCD_H * sizeof(uint16_t) + 16,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t iocfg = {
        .cs_gpio_num       = PIN_LCD_CS,
        .dc_gpio_num       = PIN_LCD_DC,
        .spi_mode          = 0,
        .pclk_hz           = LCD_SPI_HZ,
        .trans_queue_depth = 4,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .on_color_trans_done = on_flush_done,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &iocfg, &io));

    for (size_t i = 0; i < sizeof(ili_init) / sizeof(ili_init[0]); i++) {
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, ili_init[i].cmd,
                        ili_init[i].len ? ili_init[i].data : NULL, ili_init[i].len));
        if (ili_init[i].delay_ms) vTaskDelay(pdMS_TO_TICKS(ili_init[i].delay_ms));
    }
    ESP_LOGI(TAG, "ILI9341V initialised (%dx%d @ %d MHz)", LCD_W, LCD_H, LCD_SPI_HZ / 1000000);

    disp_clear(C_BLACK);
    disp_flush();
    disp_backlight(true);
}

void disp_wait_flush(void)
{
    if (flush_pending) {
        xSemaphoreTake(flush_sem, portMAX_DELAY);
        flush_pending = false;
    }
}

static void start_transfer(const uint16_t *buf)
{
    uint8_t ca[4] = {0, 0, (LCD_W - 1) >> 8, (LCD_W - 1) & 0xFF};
    uint8_t pa[4] = {0, 0, (LCD_H - 1) >> 8, (LCD_H - 1) & 0xFF};
    esp_lcd_panel_io_tx_param(io, 0x2A, ca, 4);
    esp_lcd_panel_io_tx_param(io, 0x2B, pa, 4);
    esp_lcd_panel_io_tx_color(io, 0x2C, buf, LCD_W * LCD_H * sizeof(uint16_t));
    flush_pending = true;
}

void disp_flush(void)
{
    disp_wait_flush();
    start_transfer(fb);
    disp_wait_flush();
}

void disp_flush_async(void)
{
    disp_wait_flush();              // previous frame has had a full frame to finish
    start_transfer(fb);
    fb = (fb == fb_a) ? fb_b : fb_a;   // draw the next frame into the idle buffer
}

void disp_clear(uint16_t c)
{
    if (c == 0) { memset(fb, 0, LCD_W * LCD_H * sizeof(uint16_t)); return; }
    for (int i = 0; i < LCD_W * LCD_H; i++) fb[i] = c;
}

inline void disp_px(int x, int y, uint16_t c)
{
    if ((unsigned)x < LCD_W && (unsigned)y < LCD_H) fb[y * LCD_W + x] = c;
}

void disp_fill_rect(int x, int y, int w, int h, uint16_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;
    if (w <= 0 || h <= 0) return;
    for (int row = y; row < y + h; row++) {
        uint16_t *p = &fb[row * LCD_W + x];
        for (int col = 0; col < w; col++) p[col] = c;
    }
}

void disp_rect(int x, int y, int w, int h, uint16_t c)
{
    disp_fill_rect(x, y, w, 1, c);
    disp_fill_rect(x, y + h - 1, w, 1, c);
    disp_fill_rect(x, y, 1, h, c);
    disp_fill_rect(x + w - 1, y, 1, h, c);
}

void disp_fill_circle(int cx, int cy, int r, uint16_t c)
{
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
        disp_fill_rect(cx - dx, cy + dy, dx * 2 + 1, 1, c);
    }
}

void disp_circle(int cx, int cy, int r, uint16_t c)
{
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        disp_px(cx + x, cy + y, c); disp_px(cx + y, cy + x, c);
        disp_px(cx - y, cy + x, c); disp_px(cx - x, cy + y, c);
        disp_px(cx - x, cy - y, c); disp_px(cx - y, cy - x, c);
        disp_px(cx + y, cy - x, c); disp_px(cx + x, cy - y, c);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

int disp_text_width(const char *s, int scale) { return (int)strlen(s) * 6 * scale; }

void disp_text(int x, int y, const char *s, uint16_t c, int scale)
{
    for (; *s; s++) {
        if ((unsigned char)*s < FONT_FIRST || (unsigned char)*s > FONT_LAST) { x += 6 * scale; continue; }
        const uint8_t *g = &font5x7[(*s - FONT_FIRST) * 5];
        for (int col = 0; col < 5; col++) {
            uint8_t bits = g[col];
            for (int row = 0; row < 7; row++) {
                if (bits & (1 << row)) {
                    if (scale == 1) disp_px(x + col, y + row, c);
                    else disp_fill_rect(x + col * scale, y + row * scale, scale, scale, c);
                }
            }
        }
        x += 6 * scale;
    }
}

void disp_text_center(int cy, const char *s, uint16_t c, int scale)
{
    disp_text((LCD_W - disp_text_width(s, scale)) / 2, cy, s, c, scale);
}


// ---------------- blending & effects ----------------
// Storage is byte-swapped RGB565, so each helper swaps in, works in native
// space, and swaps back. __builtin_bswap16 is a single instruction.

static inline void unpack(uint16_t sw, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint16_t v = __builtin_bswap16(sw);
    *r = (v >> 11) & 0x1F; *g = (v >> 5) & 0x3F; *b = v & 0x1F;
}
static inline uint16_t pack(uint8_t r, uint8_t g, uint8_t b)
{
    return __builtin_bswap16((uint16_t)((r << 11) | (g << 5) | b));
}

uint16_t disp_lerp(uint16_t a, uint16_t b, uint8_t t)
{
    uint8_t ar, ag, ab, br, bg, bb;
    unpack(a, &ar, &ag, &ab); unpack(b, &br, &bg, &bb);
    return pack(ar + (((int)br - ar) * t >> 8),
                ag + (((int)bg - ag) * t >> 8),
                ab + (((int)bb - ab) * t >> 8));
}

void disp_blend_px(int x, int y, uint16_t c, uint8_t a)
{
    if ((unsigned)x >= LCD_W || (unsigned)y >= LCD_H) return;
    if (a >= 255) { fb[y * LCD_W + x] = c; return; }
    if (a == 0) return;
    fb[y * LCD_W + x] = disp_lerp(fb[y * LCD_W + x], c, a);
}

void disp_blend_rect(int x, int y, int w, int h, uint16_t c, uint8_t a)
{
    if (a >= 255) { disp_fill_rect(x, y, w, h, c); return; }
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;
    for (int row = y; row < y + h; row++)
        for (int col = x; col < x + w; col++)
            fb[row * LCD_W + col] = disp_lerp(fb[row * LCD_W + col], c, a);
}

// Saturating additive blend - what makes overlapping neon read as *brighter*
// rather than just replacing whatever was underneath.
void disp_add_px(int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    if ((unsigned)x >= LCD_W || (unsigned)y >= LCD_H) return;
    uint8_t dr, dg, db;
    unpack(fb[y * LCD_W + x], &dr, &dg, &db);
    int nr = dr + r, ng = dg + g, nb = db + b;
    fb[y * LCD_W + x] = pack(nr > 31 ? 31 : nr, ng > 63 ? 63 : ng, nb > 31 ? 31 : nb);
}

void disp_glow(int cx, int cy, int r, uint16_t c, uint8_t a)
{
    uint8_t cr, cg, cb;
    unpack(c, &cr, &cg, &cb);
    int r2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        int yy = cy + dy;
        if ((unsigned)yy >= LCD_H) continue;
        for (int dx = -r; dx <= r; dx++) {
            int xx = cx + dx;
            if ((unsigned)xx >= LCD_W) continue;
            int d2 = dx * dx + dy * dy;
            if (d2 > r2) continue;
            int f = ((r2 - d2) * a) / r2;          // quadratic falloff
            disp_add_px(xx, yy, (cr * f) >> 8, (cg * f) >> 8, (cb * f) >> 8);
        }
    }
}

void disp_vgradient(int y, int h, uint16_t top, uint16_t bot)
{
    for (int i = 0; i < h; i++) {
        int yy = y + i;
        if ((unsigned)yy >= LCD_H) continue;
        uint16_t c = disp_lerp(top, bot, (uint8_t)(i * 255 / (h > 1 ? h - 1 : 1)));
        uint16_t *p = &fb[yy * LCD_W];
        for (int x = 0; x < LCD_W; x++) p[x] = c;
    }
}

void disp_line(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        disp_px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void disp_thick_line(int x0, int y0, int x1, int y1, int w, uint16_t c)
{
    for (int o = -w / 2; o <= w / 2; o++) disp_line(x0 + o, y0, x1 + o, y1, c);
}

// Cheap faux-bloom: dark offset copy underneath, then the glyph on top.
void disp_text_glow(int x, int y, const char *s, uint16_t c, int scale)
{
    uint16_t halo = disp_lerp(C_BLACK, c, 110);
    disp_text(x - 1, y, s, halo, scale);
    disp_text(x + 1, y, s, halo, scale);
    disp_text(x, y - 1, s, halo, scale);
    disp_text(x, y + 1, s, halo, scale);
    disp_text(x, y, s, c, scale);
}
