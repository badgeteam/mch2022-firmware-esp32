//This driver uses the ESP32 RMT peripheral to drive "Neopixel" compatible LEDs
//The usage of the RMT peripheral has been implemented using work by JSchaenzie:
//you can find his work at https://github.com/JSchaenzle/ESP32-NeoPixel-WS2812-RMT

#include <sdkconfig.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_err.h>
#include <esp_log.h>

#include <driver/gpio.h>
#include <driver/rmt.h>

static const char *TAG = "ws2812";

#define WS2812_RMT_CHANNEL RMT_CHANNEL_0

#define T0H 14  // 0 bit high time
#define T1H 52  // 1 bit high time
#define TL  52  // low time for either bit

static bool   gActive       = false;
rmt_item32_t* gBuffer       = NULL;
int           gBufferLength = 0;
gpio_num_t    gPin;

esp_err_t ws2812_init(gpio_num_t aGpioPin) {
    if (gActive) return ESP_OK;
    gpio_config_t io_conf = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1LL << aGpioPin,
        .pull_down_en = 0,
        .pull_up_en   = 0,
    };
    esp_err_t res = gpio_config(&io_conf);
    if (res != ESP_OK) return res;
    rmt_config_t config;
    config.rmt_mode = RMT_MODE_TX;
    config.channel = WS2812_RMT_CHANNEL;
    config.gpio_num = aGpioPin;
    config.mem_block_num = 3;
    config.tx_config.loop_en = false;
    config.tx_config.carrier_en = false;
    config.tx_config.idle_output_en = true;
    config.tx_config.idle_level = 0;
    config.clk_div = 2;
    res = rmt_config(&config);
    if (res != ESP_OK) return res;
    res = rmt_driver_install(config.channel, 0, 0);
    if (res != ESP_OK) return res;
    gActive = true;
    gPin = aGpioPin;
    return ESP_OK;
}

esp_err_t ws2812_deinit(void) {
    if (!gActive) return ESP_OK;
    esp_err_t res = rmt_driver_uninstall(WS2812_RMT_CHANNEL);
    if (res != ESP_OK) return res;
    gpio_config_t io_conf = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = 1LL << gPin,
        .pull_down_en = 0,
        .pull_up_en   = 0,
    };
    res = gpio_config(&io_conf);
    if (res != ESP_OK) return res;
    gActive = false;
    return ESP_OK;
}

esp_err_t ws2812_prepare_data(uint8_t *data, int len)
{
    if (gBuffer != NULL) return ESP_FAIL;
    gBuffer = calloc(len * 8, sizeof(rmt_item32_t));
    if (gBuffer == NULL) return ESP_FAIL;
    gBufferLength = len * 8;
    for (uint32_t pos = 0; pos < len; pos++) {
        uint32_t mask = 1 << 7;
        for (uint8_t i = 0; i < 8; i++) {
            bool bit = data[pos] & mask;
            gBuffer[pos*8 + i] = bit ?
                    (rmt_item32_t){{{T1H, 1, TL, 0}}} :
                    (rmt_item32_t){{{T0H, 1, TL, 0}}};
            mask >>= 1;
        }
    }
    return ESP_OK;
}

esp_err_t ws2812_free_data() {
    if (!gBuffer) return ESP_FAIL;
    free(gBuffer);
    gBuffer = NULL;
    gBufferLength = 0;
    return ESP_OK;
}

esp_err_t ws2812_send_data(uint8_t *data, int len)
{
    if (!gActive) return ESP_FAIL;
    esp_err_t res = ws2812_prepare_data(data, len);
    if (res != ESP_OK) return res;
    res = rmt_write_items(WS2812_RMT_CHANNEL, gBuffer, gBufferLength, false);
    if (res != ESP_OK) {
        ws2812_free_data();
        return res;
    }
    res = rmt_wait_tx_done(WS2812_RMT_CHANNEL, portMAX_DELAY);
    if (res != ESP_OK) {
        ws2812_free_data();
        return res;
    }
    res = ws2812_free_data();
    return res;
}
