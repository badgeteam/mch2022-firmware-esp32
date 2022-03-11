/**
 * Copyright (c) 2022 Nicolai Electronics
 *
 * SPDX-License-Identifier: MIT
 */

#include <sdkconfig.h>
#include <driver/gpio.h>
#include "rp2040.h"
#include "managed_i2c.h"

static const char *TAG = "RP2040";

esp_err_t rp2040_init(RP2040* device) {
    esp_err_t res;

    uint8_t firmware_version;
    res = rp2040_get_firmware_version(device, &firmware_version);
    if (res != ESP_OK) return res;

    if (firmware_version != 1) {
        ESP_LOGE(TAG, "Unsupported RP2040 firmware version (%u) found", firmware_version);
        return ESP_ERR_INVALID_VERSION;
    }

    res = i2c_read_reg(device->i2c_bus, device->i2c_address, RP2040_REG_GPIO_DIR, &device->_gpio_direction, 1);
    if (res != ESP_OK) return res;

    res = i2c_read_reg(device->i2c_bus, device->i2c_address, RP2040_REG_GPIO_OUT, &device->_gpio_value, 1);
    if (res != ESP_OK) return res;

    return ESP_OK;
}

esp_err_t rp2040_get_firmware_version(RP2040* device, uint8_t* version) {
    return i2c_read_reg(device->i2c_bus, device->i2c_address, RP2040_REG_FW_VER, version, 1);
}

esp_err_t rp2040_get_gpio_dir(RP2040* device, uint8_t gpio, bool* direction) {
    esp_err_t res = i2c_read_reg(device->i2c_bus, device->i2c_address, RP2040_REG_GPIO_DIR, &device->_gpio_direction, 1);
    if (res != ESP_OK) return res;
    *direction = (device->_gpio_direction >> gpio) & 0x01;
    return ESP_OK;
}
esp_err_t rp2040_set_gpio_dir(RP2040* device, uint8_t gpio, bool direction) {
    if (direction) {
        device->_gpio_direction |= 1UL << gpio;
    } else {
        device->_gpio_direction &= ~(1UL << gpio);
    }
    return i2c_write_reg_n(device->i2c_bus, device->i2c_address, RP2040_REG_GPIO_DIR, &device->_gpio_direction, 1);
}

esp_err_t rp2040_get_gpio_value(RP2040* device, uint8_t gpio, bool* value) {
    uint8_t reg_value;
    esp_err_t res = i2c_read_reg(device->i2c_bus, device->i2c_address, RP2040_REG_GPIO_IN, &reg_value, 1);
    if (res != ESP_OK) return res;
    *value = (reg_value >> gpio) & 0x01;
    return ESP_OK;    
}

esp_err_t rp2040_set_gpio_value(RP2040* device, uint8_t gpio, bool value) {
    if (value) {
        device->_gpio_value |= 1UL << gpio;
    } else {
        device->_gpio_value &= ~(1UL << gpio);
    }
    return i2c_write_reg_n(device->i2c_bus, device->i2c_address, RP2040_REG_GPIO_OUT, &device->_gpio_value, 1);
}

esp_err_t rp2040_get_lcd_mode(RP2040* device, lcd_mode_t* mode) {
    return i2c_read_reg(device->i2c_bus, device->i2c_address, RP2040_REG_LCD_MODE, (uint8_t*) mode, 1);
}

esp_err_t rp2040_set_lcd_mode(RP2040* device, lcd_mode_t mode) {
    esp_err_t res;
    lcd_mode_t verification;
    do {
        res = i2c_write_reg_n(device->i2c_bus, device->i2c_address, RP2040_REG_LCD_MODE, (uint8_t*) &mode, 1);
        if (res != ESP_OK) return res;
        res = rp2040_get_lcd_mode(device, &verification);
        if (res != ESP_OK) return res;
    } while (verification != mode);
    return res;
}

esp_err_t rp2040_get_lcd_backlight(RP2040* device, uint8_t* brightness) {
    return i2c_read_reg(device->i2c_bus, device->i2c_address, RP2040_REG_LCD_BACKLIGHT, brightness, 1);
}

esp_err_t rp2040_set_lcd_backlight(RP2040* device, uint8_t brightness) {
    return i2c_write_reg_n(device->i2c_bus, device->i2c_address, RP2040_REG_LCD_BACKLIGHT, &brightness, 1);
}

esp_err_t rp2040_get_led_mode(RP2040* device, bool* enabled, bool* automatic_flush) {
    uint8_t value;
    esp_err_t res = i2c_read_reg(device->i2c_bus, device->i2c_address, RP2040_REG_LED_MODE, &value, 1);
    if (res != ESP_OK) return res;
    *enabled = (value >> 0) & 0x01;
    *automatic_flush = (value >> 1) & 0x01;
    return ESP_OK;
}

esp_err_t rp2040_set_led_mode(RP2040* device, bool enabled, bool automatic_flush) {
    uint8_t value = enabled | (automatic_flush << 1);
    return i2c_write_reg_n(device->i2c_bus, device->i2c_address, RP2040_REG_LED_MODE, &value, 1);
}

esp_err_t rp2040_get_led_value(RP2040* device, uint8_t led, uint8_t* red, uint8_t* green, uint8_t* blue) {
    if (led > 5) return ESP_ERR_NOT_FOUND;
    uint8_t value[3];
    esp_err_t res = i2c_read_reg(device->i2c_bus, device->i2c_address, RP2040_REG_LED_R0 + (led * 3), value, 3);
    if (res != ESP_OK) return res;
    *red = value[0];
    *green = value[1];
    *blue = value[2];
    return ESP_OK;
}

esp_err_t rp2040_set_led_value(RP2040* device, uint8_t led, uint8_t red, uint8_t green, uint8_t blue) {
    if (led > 5) return ESP_ERR_NOT_FOUND;
    uint8_t value[3];
    value[0] = red;
    value[1] = green;
    value[2] = blue;
    return i2c_write_reg_n(device->i2c_bus, device->i2c_address, RP2040_REG_LED_R0 + (led * 3), value, 3);
}

esp_err_t rp2040_get_led_values(RP2040* device, uint8_t* buffer) {
    return i2c_read_reg(device->i2c_bus, device->i2c_address, RP2040_REG_LED_R0, buffer, 15);
}

esp_err_t rp2040_set_led_values(RP2040* device, uint8_t* buffer) {
    return i2c_write_reg_n(device->i2c_bus, device->i2c_address, RP2040_REG_LED_R0, buffer, 15);
}
