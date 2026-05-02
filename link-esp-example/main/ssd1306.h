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

// row: 0..(8/scale_y - 1)  |  center: true = centre-align, false = left-align
// scale_x: horizontal pixel repeat (1 = 5px wide, 2 = 10px, …)
// scale_y: vertical pixel repeat  (1 = 8px tall, 2 = 16px, …)
void ssd1306_write_string(uint8_t row, const char *str, bool center, uint8_t scale_x, uint8_t scale_y);

#ifdef __cplusplus
}
#endif
