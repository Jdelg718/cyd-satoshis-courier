#include "rgbled.h"
#include "board.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"

static rmt_channel_handle_t chan = NULL;
static rmt_encoder_handle_t enc  = NULL;

void rgbled_init(void)
{
    rmt_tx_channel_config_t cc = {
        .gpio_num = PIN_RGB_LED,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,     // 0.1 us per tick
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&cc, &chan));

    // WS2812B: 0 = 0.3us high / 0.9us low, 1 = 0.9us high / 0.3us low
    rmt_bytes_encoder_config_t bc = {
        .bit0 = { .level0 = 1, .duration0 = 3, .level1 = 0, .duration1 = 9 },
        .bit1 = { .level0 = 1, .duration0 = 9, .level1 = 0, .duration1 = 3 },
        .flags.msb_first = 1,
    };
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&bc, &enc));
    ESP_ERROR_CHECK(rmt_enable(chan));
    rgbled_set(0, 0, 0);
}

void rgbled_set(uint8_t r, uint8_t g, uint8_t b)
{
    if (!chan) return;
    uint8_t grb[3] = { g, r, b };                 // WS2812 wants GRB order
    rmt_transmit_config_t tc = { .loop_count = 0 };
    rmt_transmit(chan, enc, grb, sizeof(grb), &tc);
    rmt_tx_wait_all_done(chan, 100);
}
