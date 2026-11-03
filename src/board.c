#include "board.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "board";
static adc_oneshot_unit_handle_t adc1 = NULL;

void board_init(void)
{
    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << PIN_BOOT_BTN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&btn);

    adc_oneshot_unit_init_cfg_t uc = { .unit_id = ADC_UNIT_1 };
    if (adc_oneshot_new_unit(&uc, &adc1) == ESP_OK) {
        adc_oneshot_chan_cfg_t ch = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
        adc_oneshot_config_channel(adc1, ADC_CHANNEL_8, &ch);   // IO9  battery
        adc_oneshot_config_channel(adc1, ADC_CHANNEL_1, &ch);   // IO2  floating
        adc_oneshot_config_channel(adc1, ADC_CHANNEL_2, &ch);   // IO3  floating
    }
}

bool board_boot_pressed(void) { return gpio_get_level(PIN_BOOT_BTN) == 0; }

int board_adc_raw(adc_channel_t ch)
{
    int v = -1;
    if (adc1) adc_oneshot_read(adc1, ch, &v);
    return v;
}

float board_battery_volts(void)
{
    int raw = board_adc_raw(ADC_CHANNEL_8);
    if (raw < 0) return -1.0f;
    // 2:1 divider on the BAT input; 12dB atten ~ 3.1V full scale.
    return (raw / 4095.0f) * 3.1f * 2.0f;
}
