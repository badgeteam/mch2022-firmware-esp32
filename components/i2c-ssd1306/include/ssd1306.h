/**
 * Copyright (c) 2023 Nicolai Electronics
 *
 * SPDX-License-Identifier: MIT
 */


#pragma once

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>

typedef struct SSD1306 {
    int pin_reset;
    int i2c_bus;
    int i2c_address;
} SSD1306;

esp_err_t ssd1306_init(SSD1306* device);
esp_err_t ssd1306_write_part(SSD1306* device, const uint8_t *buffer, uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1);
esp_err_t ssd1306_write(SSD1306* device, const uint8_t *buffer);

#ifdef __cplusplus
}
#endif //__cplusplus
