#pragma once

#include <esp_err.h>
#include <stdint.h>

#define BME680_REG_RESET   0xE0
#define BME680_REG_CHIP_ID 0xD0

#define BME680_CHIP_ID 0x61

typedef struct BME680 {
    int              i2c_bus;
    int              i2c_address;
} BME680;

esp_err_t bme680_init(BME680* device);
esp_err_t bme680_deinit(BME680* device);
esp_err_t bme680_check_id(BME680* device);
esp_err_t bme680_reset(BME680* device);
