/**
 * Copyright (c) 2022 Nicolai Electronics
 *
 * SPDX-License-Identifier: MIT
 */

#include <sdkconfig.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "bme680.h"
#include "managed_i2c.h"

static const char *TAG = "BME680";

esp_err_t bme680_check_id(BME680* device) {
    uint8_t chip_id;
    esp_err_t res = i2c_read_reg(device->i2c_bus, device->i2c_address, BME680_REG_CHIP_ID, &chip_id, 1);
    if (res != ESP_OK) return res;
    if (chip_id != BME680_CHIP_ID) {
        ESP_LOGE(TAG, "Unexpected chip id value 0x%02X, expected 0x%02X", chip_id, BME680_CHIP_ID);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t bme680_reset(BME680* device) {
    uint8_t value = 0xFF;
    esp_err_t res = i2c_write_reg_n(device->i2c_bus, device->i2c_address, BME680_REG_RESET, &value, 1);
    if (res != ESP_OK) return res;
    return ESP_OK;
}

esp_err_t bme680_init(BME680* device) {
    esp_err_t res = bme680_reset(device);
    if (res != ESP_OK) return res;
    vTaskDelay(100 / portTICK_PERIOD_MS);
    res = bme680_check_id(device);
    if (res != ESP_OK) return res;
    return res;
}

esp_err_t bme680_deinit(BME680* device) {
    return bme680_reset(device);
}
