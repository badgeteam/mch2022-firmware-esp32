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

inline void _send_input_change(RP2040* device, uint8_t input, bool value) {
    rp2040_input_message_t message;
    message.input = input;
    message.state = value;
    xQueueSend(device->queue, &message, portMAX_DELAY);
}

void rp2040_intr_task(void *arg) {
    RP2040* device = (RP2040*) arg;
    uint32_t state;
    
    while (1) {
        if (xSemaphoreTake(device->_intr_trigger, portMAX_DELAY)) {
            esp_err_t res = i2c_read_reg(device->i2c_bus, device->i2c_address, RP2040_REG_INPUT1, (uint8_t*) &state, 4);
            if (res != ESP_OK) {
                ESP_LOGE(TAG, "RP2040 interrupt task failed to read from RP2040");
                continue;
            }
            //ESP_LOGW(TAG, "RP2040 input state %08x", state);
            uint16_t interrupt = state >> 16;
            uint16_t values    = state & 0xFFFF;
            for (uint8_t index = 0; index < 16; index++) {
                if ((interrupt >> index) & 0x01) {
                    _send_input_change(device, index, (values >> index) & 0x01);
                }
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }
}

void rp2040_intr_handler(void *arg) {
    /* in interrupt handler context */
    RP2040* device = (RP2040*) arg;
    xSemaphoreGiveFromISR(device->_intr_trigger, NULL);
}

esp_err_t rp2040_init(RP2040* device) {
    esp_err_t res;

    res = rp2040_get_firmware_version(device, &device->_fw_version);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read firmware version");
        return res;
    }

    if (device->_fw_version < 1) {
        ESP_LOGE(TAG, "Unsupported RP2040 firmware version (%u) found", device->_fw_version);
        return ESP_ERR_INVALID_VERSION;
    }

    res = i2c_read_reg(device->i2c_bus, device->i2c_address, RP2040_REG_GPIO_DIR, &device->_gpio_direction, 1);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read GPIO direction");
        return res;
    }

    res = i2c_read_reg(device->i2c_bus, device->i2c_address, RP2040_REG_GPIO_OUT, &device->_gpio_value, 1);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read GPIO state");
        return res;
    }
    
    //Create interrupt trigger
    device->_intr_trigger = xSemaphoreCreateBinary();
    if (device->_intr_trigger == NULL) return ESP_ERR_NO_MEM;
    
    //Attach interrupt to interrupt pin
    if (device->pin_interrupt >= 0) {
        res = gpio_isr_handler_add(device->pin_interrupt, rp2040_intr_handler, (void*) device);
        if (res != ESP_OK) return res;

        gpio_config_t io_conf = {
            .intr_type    = GPIO_INTR_NEGEDGE,
            .mode         = GPIO_MODE_INPUT,
            .pin_bit_mask = 1LL << device->pin_interrupt,
            .pull_down_en = 0,
            .pull_up_en   = 1,
        };

        res = gpio_config(&io_conf);
        if (res != ESP_OK) return res;
        
        xTaskCreate(&rp2040_intr_task, "RP2040 interrupt", 4096, (void*) device, 10, &device->_intr_task_handle);
        xSemaphoreGive(device->_intr_trigger);
    }

    return ESP_OK;
}

esp_err_t rp2040_get_firmware_version(RP2040* device, uint8_t* version) {
    return i2c_read_reg(device->i2c_bus, device->i2c_address, RP2040_REG_FW_VER, version, 1);
}

esp_err_t rp2040_get_bootloader_version(RP2040* device, uint8_t* version) {
    if (device->_fw_version != 0xFF) return ESP_FAIL;
    return i2c_read_reg(device->i2c_bus, device->i2c_address, RP2040_BL_REG_BL_VER, version, 1);
}

esp_err_t rp2040_get_bootloader_state(RP2040* device, uint8_t* state) {
    if (device->_fw_version != 0xFF) return ESP_FAIL;
    return i2c_read_reg(device->i2c_bus, device->i2c_address, RP2040_BL_REG_BL_STATE, state, 1);
}

esp_err_t rp2040_set_bootloader_ctrl(RP2040* device, uint8_t action) {
    if (device->_fw_version != 0xFF) return ESP_FAIL;
    return i2c_write_reg_n(device->i2c_bus, device->i2c_address, RP2040_BL_REG_BL_CTRL, &action, 1);
}

esp_err_t rp2040_reboot_to_bootloader(RP2040* device) {
    if ((device->_fw_version < 0x01) && (device->_fw_version >= 0xFF)) return ESP_FAIL;
    uint8_t value = 0xBE;
    return i2c_write_reg_n(device->i2c_bus, device->i2c_address, RP2040_REG_BL_TRIGGER, &value, 1);
}

esp_err_t rp2040_get_gpio_dir(RP2040* device, uint8_t gpio, bool* direction) {
    if ((device->_fw_version < 0x01) && (device->_fw_version >= 0xFF)) return ESP_FAIL;
    esp_err_t res = i2c_read_reg(device->i2c_bus, device->i2c_address, RP2040_REG_GPIO_DIR, &device->_gpio_direction, 1);
    if (res != ESP_OK) return res;
    *direction = (device->_gpio_direction >> gpio) & 0x01;
    return ESP_OK;
}
esp_err_t rp2040_set_gpio_dir(RP2040* device, uint8_t gpio, bool direction) {
    if ((device->_fw_version < 0x01) && (device->_fw_version >= 0xFF)) return ESP_FAIL;
    if (direction) {
        device->_gpio_direction |= 1UL << gpio;
    } else {
        device->_gpio_direction &= ~(1UL << gpio);
    }
    return i2c_write_reg_n(device->i2c_bus, device->i2c_address, RP2040_REG_GPIO_DIR, &device->_gpio_direction, 1);
}

esp_err_t rp2040_get_gpio_value(RP2040* device, uint8_t gpio, bool* value) {
    if ((device->_fw_version < 0x01) && (device->_fw_version >= 0xFF)) return ESP_FAIL;
    uint8_t reg_value;
    esp_err_t res = i2c_read_reg(device->i2c_bus, device->i2c_address, RP2040_REG_GPIO_IN, &reg_value, 1);
    if (res != ESP_OK) return res;
    *value = (reg_value >> gpio) & 0x01;
    return ESP_OK;    
}

esp_err_t rp2040_set_gpio_value(RP2040* device, uint8_t gpio, bool value) {
    if ((device->_fw_version < 0x01) && (device->_fw_version >= 0xFF)) return ESP_FAIL;
    if (value) {
        device->_gpio_value |= 1UL << gpio;
    } else {
        device->_gpio_value &= ~(1UL << gpio);
    }
    return i2c_write_reg_n(device->i2c_bus, device->i2c_address, RP2040_REG_GPIO_OUT, &device->_gpio_value, 1);
}

esp_err_t rp2040_get_lcd_backlight(RP2040* device, uint8_t* brightness) {
    if ((device->_fw_version < 0x01) && (device->_fw_version >= 0xFF)) return ESP_FAIL;
    return i2c_read_reg(device->i2c_bus, device->i2c_address, RP2040_REG_LCD_BACKLIGHT, brightness, 1);
}

esp_err_t rp2040_set_lcd_backlight(RP2040* device, uint8_t brightness) {
    if ((device->_fw_version < 0x01) && (device->_fw_version >= 0xFF)) return ESP_OK; // Ignore if unsupported
    return i2c_write_reg_n(device->i2c_bus, device->i2c_address, RP2040_REG_LCD_BACKLIGHT, &brightness, 1);
}

esp_err_t rp2040_set_fpga(RP2040* device, bool enabled) {
    if ((device->_fw_version < 0x01) && (device->_fw_version >= 0xFF)) return ESP_FAIL;
    uint8_t value = enabled ? 0x01 : 0x00;
    return i2c_write_reg_n(device->i2c_bus, device->i2c_address, RP2040_REG_FPGA, &value, 1);
}

esp_err_t rp2040_read_buttons(RP2040* device, uint16_t* value) {
    if ((device->_fw_version < 0x01) && (device->_fw_version >= 0xFF)) return ESP_FAIL;
    return i2c_read_reg(device->i2c_bus, device->i2c_address, RP2040_REG_INPUT1, (uint8_t*) value, 2);
}
