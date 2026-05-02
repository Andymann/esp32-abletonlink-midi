#pragma once
#include <stdbool.h>
#include <stdint.h>

// SSD1306 128x64 I2C driver
// SDA = GPIO21, SCL = GPIO22  (change in ssd1306.c if needed)

#ifdef __cplusplus
extern "C" {
#endif

void ssd1306_init(void);
void ssd1306_clear(void);

// row: 0-3  |  center: true = centre-align, false = left-align
void ssd1306_write_string(uint8_t row, const char *str, bool center);

#ifdef __cplusplus
}
#endif
