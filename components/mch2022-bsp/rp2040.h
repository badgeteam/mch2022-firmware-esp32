#pragma once

#include <esp_err.h>
#include <esp_log.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

enum {
    RP2040_REG_FW_VER = 0,
    RP2040_REG_GPIO_DIR,
    RP2040_REG_GPIO_IN,
    RP2040_REG_GPIO_OUT,
    RP2040_REG_LCD_MODE,
    RP2040_REG_LCD_BACKLIGHT,
    RP2040_REG_LED_MODE,
    RP2040_REG_LED_R0,
    RP2040_REG_LED_G0,
    RP2040_REG_LED_B0,
    RP2040_REG_LED_R1,
    RP2040_REG_LED_G1,
    RP2040_REG_LED_B1,
    RP2040_REG_LED_R2,
    RP2040_REG_LED_G2,
    RP2040_REG_LED_B2,
    RP2040_REG_LED_R3,
    RP2040_REG_LED_G3,
    RP2040_REG_LED_B3,
    RP2040_REG_LED_R4,
    RP2040_REG_LED_G4,
    RP2040_REG_LED_B4,
};

typedef enum {
    LCD_MODE_SPI = 0,
    LCD_MODE_PARALLEL = 1
} lcd_mode_t;

typedef void (*rp2040_intr_t)();

typedef struct {
    int              i2c_bus;
    int              i2c_address;
    int              pin_interrupt;
    rp2040_intr_t    _intr_handler;
    TaskHandle_t     _intr_task_handle;
    xSemaphoreHandle _intr_trigger;
    xSemaphoreHandle _mux;
    uint8_t          _gpio_direction;
    uint8_t          _gpio_value;
} RP2040;

esp_err_t rp2040_init(RP2040* device);

esp_err_t rp2040_get_firmware_version(RP2040* device, uint8_t* version);

esp_err_t rp2040_get_gpio_dir(RP2040* device, uint8_t gpio, bool* direction);
esp_err_t rp2040_set_gpio_dir(RP2040* device, uint8_t gpio, bool direction);

esp_err_t rp2040_get_gpio_value(RP2040* device, uint8_t gpio, bool* value);
esp_err_t rp2040_set_gpio_value(RP2040* device, uint8_t gpio, bool value);

esp_err_t rp2040_get_lcd_mode(RP2040* device, lcd_mode_t* mode);
esp_err_t rp2040_set_lcd_mode(RP2040* device, lcd_mode_t mode);

esp_err_t rp2040_get_lcd_backlight(RP2040* device, uint8_t* brightness);
esp_err_t rp2040_set_lcd_backlight(RP2040* device, uint8_t brightness);

esp_err_t rp2040_get_led_mode(RP2040* device, bool* enabled, bool* automatic_flush);
esp_err_t rp2040_set_led_mode(RP2040* device, bool enabled, bool automatic_flush);

esp_err_t rp2040_get_led_value(RP2040* device, uint8_t led, uint8_t* red, uint8_t* green, uint8_t* blue);
esp_err_t rp2040_set_led_value(RP2040* device, uint8_t led, uint8_t red, uint8_t green, uint8_t blue);

esp_err_t rp2040_get_led_values(RP2040* device, uint8_t* buffer); // Expects a buffer that can fit 15 bytes (R, G, B * 5 LEDs)
esp_err_t rp2040_set_led_values(RP2040* device, uint8_t* buffer); // Expects a buffer that can contains 15 bytes (R, G, B * 5 LEDs)
