#pragma once

#include <stdint.h>
#include <esp_err.h>
#include <driver/gpio.h>

__BEGIN_DECLS

/**
 * Initialize the leds driver. (configure SPI bus and GPIO pins)
 * @return ESP_OK on success; any other value indicates an error
 */
extern esp_err_t ws2812_init(gpio_num_t aGpioPin);

/**
 * Enable power to the leds bar.
 * @return ESP_OK on success; any other value indicates an error
 */
extern esp_err_t ws2812_enable(int gpio_pin);

/**
 * Disable power to the leds bar.
 * @return ESP_OK on success; any other value indicates an error
 */
extern esp_err_t ws2812_disable(void);

/**
 * Send color-data to the leds bus.
 * @param data the data-bytes to send on the bus.
 * @param len the data-length.
 * @return ESP_OK on success; any other value indicates an error
 */
extern esp_err_t ws2812_send_data(uint8_t *data, int len);

__END_DECLS
