#include "touch.h"
#include "board.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "touch";
static i2c_master_bus_handle_t bus = NULL;
static i2c_master_dev_handle_t dev = NULL;

void *touch_i2c_bus(void) { return (void *)bus; }

static esp_err_t rd(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(dev, &reg, 1, buf, n, pdMS_TO_TICKS(100));
}

void touch_init(void)
{
    i2c_master_bus_config_t bc = {
        .i2c_port = TOUCH_I2C_PORT,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &bus));

    // Hardware reset the FT6336G: low 10ms, then 300ms to boot.
    gpio_config_t rst = { .pin_bit_mask = 1ULL << PIN_TOUCH_RST, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&rst);
    gpio_set_level(PIN_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(300));

    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = FT6336_ADDR,
        .scl_speed_hz = I2C_BUS_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dc, &dev));

    uint8_t v = 0;
    if (rd(0xA8, &v, 1) == ESP_OK) ESP_LOGI(TAG, "FocalTech vendor id = 0x%02X", v);
    else ESP_LOGE(TAG, "FT6336G did not respond at 0x%02X", FT6336_ADDR);
    uint8_t cid = 0; rd(0xA3, &cid, 1);
    ESP_LOGI(TAG, "chip id = 0x%02X, firmware = ", cid);
}

uint8_t touch_probe_id(void) { uint8_t v = 0; rd(0xA8, &v, 1); return v; }

bool touch_read(touch_pt_t *pt)
{
    uint8_t b[5];
    pt->pressed = false;
    if (rd(0x02, b, 5) != ESP_OK) return false;
    uint8_t n = b[0] & 0x0F;
    if (n == 0 || n > 2) return false;
    uint16_t rx = ((b[1] & 0x0F) << 8) | b[2];
    uint16_t ry = ((b[3] & 0x0F) << 8) | b[4];
    // Panel native orientation assumed to match the LCD; verified in bring-up.
    pt->x = rx; pt->y = ry; pt->pressed = true;
    return true;
}
