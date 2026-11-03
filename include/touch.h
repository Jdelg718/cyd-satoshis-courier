#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct { int16_t x, y; bool pressed; } touch_pt_t;
void       touch_init(void);                 // also brings up the shared I2C bus
bool       touch_read(touch_pt_t *pt);
uint8_t    touch_probe_id(void);             // FocalTech vendor id, 0 if absent
void      *touch_i2c_bus(void);              // shared with ES8311
