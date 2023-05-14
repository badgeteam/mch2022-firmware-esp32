/**
* Copyright (c) 2022 Nicolai Electronics
*
* SPDX-License-Identifier: MIT
*/

#include <sdkconfig.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/i2c.h>

#include "include/ssd1306.h"

#define CTRL_CO 0x80 // Set to true if more commands follow
#define CTRL_DC 0x40 // Set to true if following byte should be stored in gdram

static inline esp_err_t i2c_command(SSD1306* device, uint8_t command, const uint8_t* data, size_t length) {
    esp_err_t res;
    i2c_cmd_handle_t cmd_handle = i2c_cmd_link_create();
    res = i2c_master_start(cmd_handle);
    if (res != ESP_OK) goto end;
    res = i2c_master_write_byte(cmd_handle, ( device->i2c_address << 1 ) | I2C_MASTER_WRITE, 1);
    if (res != ESP_OK) goto end;
    res = i2c_master_write_byte(cmd_handle, 0x00, 1);
    if (res != ESP_OK) goto end;
    res = i2c_master_write_byte(cmd_handle, command, 1);
    if (res != ESP_OK) goto end;
    for (size_t index = 0; index < length; index++) {
        res = i2c_master_write_byte(cmd_handle, data[index], 1);
        if (res != ESP_OK) goto end;
    }
    res = i2c_master_stop(cmd_handle);
    if (res != ESP_OK) goto end;
    res = i2c_master_cmd_begin(device->i2c_bus, cmd_handle, 1000 / portTICK_RATE_MS);
    end:
    i2c_cmd_link_delete(cmd_handle);
    return res;
}

static inline esp_err_t i2c_data(SSD1306* device, const uint8_t* data, uint16_t length) {
    esp_err_t res;
    i2c_cmd_handle_t cmd_handle = i2c_cmd_link_create();
    res = i2c_master_start(cmd_handle);
    if (res != ESP_OK) goto end;
    res = i2c_master_write_byte(cmd_handle, ( device->i2c_address << 1 ) | I2C_MASTER_WRITE, 1);
    if (res != ESP_OK) goto end;
    res = i2c_master_write_byte(cmd_handle, CTRL_DC, 1);
    if (res != ESP_OK) goto end;
    for (size_t index = 0; index < length; index++) {
        res = i2c_master_write_byte(cmd_handle, data[index], 1);
        if (res != ESP_OK) goto end;
    }
    res = i2c_master_stop(cmd_handle);
    if (res != ESP_OK) goto end;
    res = i2c_master_cmd_begin(device->i2c_bus, cmd_handle, 1000 / portTICK_RATE_MS);
    end:
    i2c_cmd_link_delete(cmd_handle);
    return res;
}

static inline esp_err_t reset(SSD1306* device) {
    if (device->pin_reset >= 0) {
        gpio_set_level(device->pin_reset, false);
        vTaskDelay(5 / portTICK_PERIOD_MS);
        gpio_set_level(device->pin_reset, true);
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
    return ESP_OK;
}

esp_err_t ssd1306_write_init_data(SSD1306* device, const uint8_t* data) {
    esp_err_t res = ESP_OK;
    uint8_t command, length;
    while(true) {
        command = *data++;
        if(!command) break; // End of sequence
        length = *data++;
        res = i2c_command(device, command, data, length);
        if (res != ESP_OK) break;
        data += length;
    }
    return res;
}

#define SSD1306_CMD_SET_LOWER_COLUMN_START_ADDRESS  0x00 // 0x00 - 0x0f
#define SSD1306_CMD_SET_HIGHER_COLUMN_START_ADDRESS 0x10 // 0x10 - 0x1f
#define SSD1306_CMD_SET_MEMORY_ADDRESSING_MODE      0x20
#define SSD1306_CMD_SET_COLUMN_ADDRESS              0x21
#define SSD1306_CMD_SET_PAGE_ADDRESS                0x22
#define SSD1306_CMD_SET_DISPLAY_START_LINE          0x40 // 0x40 - 0x7f
#define SSD1306_CMD_SET_CONTRAST                    0x81
#define SSD1306_CMD_SET_SEGMENT_REMAP               0xa0 // 0xa0 - 0xa1
#define SSD1306_CMD_DISPLAY_TEST_OFF                0xa4 // Resumes displaying GDDRAM contents
#define SSD1306_CMD_DISPLAY_TEST_ON                 0xa5 // Turns on all segments
#define SSD1306_CMD_DISPLAY_INVERT                  0xa5
#define SSD1306_CMD_DISPLAY_NORMAL                  0xa6
#define SSD1306_CMD_SET_MULTIPLEX_RATIO             0xa8
#define SSD1306_CMD_SET_DISPLAY_OFF                 0xae
#define SSD1306_CMD_SET_DISPLAY_ON                  0xaf
#define SSD1306_CMD_SET_PAGE_START_ADDRESS          0xb0 // 0xb0 - 0xb7
#define SSD1306_CMD_SET_COM_OUTPUT_SCAN_DIRECTION   0xc0 // 0xc0 - 0xc8
#define SSD1306_CMD_SET_DISPLAY_OFFSET              0xd3
#define SSD1306_CMD_SET_DISPLAY_CLOCK_DIVIDE_RATIO  0xd5
#define SSD1306_CMD_SET_PRECHARGE_PERIOD            0xd9
#define SSD1306_CMD_SET_COM_PINS_HARDWARE_CONFIG    0xda
#define SSD1306_CMD_SET_VCOMH_DESELECT_LEVEL        0xdb
#define SSD1306_SET_CHARGEPUMP                      0x8d
#define SSD1306_CMD_NOP                             0xe3

const uint8_t ssd1306_init_data[] = {
    SSD1306_CMD_SET_DISPLAY_OFF,                   0,       // Turn display off
    SSD1306_CMD_SET_MULTIPLEX_RATIO,               1, 0x3f, // MUX ratio: 1/64
    SSD1306_CMD_SET_DISPLAY_OFFSET,                1, 0x00, // Display offset: 0
    SSD1306_CMD_SET_DISPLAY_START_LINE        | 0, 0,       // Set display start line
    SSD1306_CMD_SET_SEGMENT_REMAP             | 1, 0,       // Set segment re-map
    SSD1306_CMD_SET_COM_OUTPUT_SCAN_DIRECTION | 8, 0,       // Set COM output scan direction
    SSD1306_CMD_SET_COM_PINS_HARDWARE_CONFIG,      1, 0x12, // Set COM pins hardware configuraiton
    SSD1306_CMD_SET_CONTRAST,                      1, 0xcf, // Set contrast control
    SSD1306_CMD_SET_DISPLAY_CLOCK_DIVIDE_RATIO,    1, 0x80, // Set osc frequency
    SSD1306_SET_CHARGEPUMP,                        1, 0x14, // Enable charge pump regulator
    SSD1306_CMD_SET_MEMORY_ADDRESSING_MODE,        1, 0x01, // Vertical addressing mode
    SSD1306_CMD_DISPLAY_NORMAL,                    0,       // Disable inverted mode
    SSD1306_CMD_SET_PRECHARGE_PERIOD,              1, 0xf1, // Set pre-charge period
    SSD1306_CMD_SET_VCOMH_DESELECT_LEVEL,          1, 0x30, // Vcomh deselect level
    SSD1306_CMD_SET_DISPLAY_ON,                    0,       // Turn display on
    0x00
};

esp_err_t ssd1306_init(SSD1306* device) {
    if (device->pin_reset >= 0) {
        gpio_set_direction(device->pin_reset, GPIO_MODE_OUTPUT);
        reset(device);
    }
    return ssd1306_write_init_data(device, ssd1306_init_data);
}

esp_err_t ssd1306_write_part(SSD1306* device, const uint8_t *buffer, uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) {
    esp_err_t res;
    uint8_t column_addresses[2] = {x0, x1};
    res = i2c_command(device, SSD1306_CMD_SET_COLUMN_ADDRESS, column_addresses, 2);
    if (res != ESP_OK) return res;
    uint8_t page_addresses[2] = {y0, y1};
    res = i2c_command(device, SSD1306_CMD_SET_PAGE_ADDRESS, page_addresses, 2);
    if (res != ESP_OK) return res;
    res = i2c_data(device, buffer, 1024);
    if ( res != ESP_OK) return res;
    return res;
}

esp_err_t ssd1306_write(SSD1306* device, const uint8_t *buffer) {
    return ssd1306_write_part(device, buffer, 0, 0, 127, 63);
}

