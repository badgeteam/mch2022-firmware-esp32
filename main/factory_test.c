#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "audio.h"
#include "fpga_test.h"
#include "hardware.h"
#include "ice40.h"
#include "pax_gfx.h"
#include "rp2040.h"
#include "settings.h"
#include "test_common.h"
#include "wifi_defaults.h"
#include "ws2812.h"

static const char* TAG = "factory";

/* Test routines */

bool test_rp2040_init(uint32_t* rc) {
    esp_err_t res = bsp_rp2040_init();
    *rc           = (uint32_t) res;
    return (res == ESP_OK);
}

bool test_ice40_init(uint32_t* rc) {
    esp_err_t res = bsp_ice40_init();
    *rc           = (uint32_t) res;
    return (res == ESP_OK);
}

bool test_bno055_init(uint32_t* rc) {
    esp_err_t res = bsp_bno055_init();
    *rc           = (uint32_t) res;
    return (res == ESP_OK);
}

bool test_bme680_init(uint32_t* rc) {
    esp_err_t res = bsp_bme680_init();
    *rc           = (uint32_t) res;
    return (res == ESP_OK);
}

bool test_stuck_buttons(uint32_t* rc) {
    RP2040*   rp2040 = get_rp2040();
    uint16_t  state;
    esp_err_t res = rp2040_read_buttons(rp2040, &state);
    if (res != ESP_OK) {
        *rc = 0xFFFFFFFF;
        return false;
    }

    state &= ~(1 << RP2040_INPUT_FPGA_CDONE);  // Ignore FPGA CDONE

    *rc = state;

    return (state == 0x0000);
}

bool test_adc_vbat(uint32_t* rc) {
    float     value = 0;
    esp_err_t res   = rp2040_read_vbat(get_rp2040(), &value);
    *rc             = value * 100;
    return ((res == ESP_OK) && (value < 4.3) && (value > 3.9));
}

bool test_adc_vusb(uint32_t* rc) {
    float     value = 0;
    esp_err_t res   = rp2040_read_vusb(get_rp2040(), &value);
    *rc             = value * 100;
    return ((res == ESP_OK) && (value > 4.5));
}

bool test_sd_power(uint32_t* rc) {
    *rc = 0x00000000;
    // Init all GPIO pins for SD card and LED
    if (gpio_reset_pin(GPIO_SD_PWR) != ESP_OK) return false;
    if (gpio_set_direction(GPIO_SD_PWR, GPIO_MODE_INPUT) != ESP_OK) return false;
    if (gpio_reset_pin(GPIO_SD_CMD) != ESP_OK) return false;
    if (gpio_set_direction(GPIO_SD_CMD, GPIO_MODE_INPUT) != ESP_OK) return false;
    if (gpio_reset_pin(GPIO_SD_CLK) != ESP_OK) return false;
    if (gpio_set_direction(GPIO_SD_CLK, GPIO_MODE_INPUT) != ESP_OK) return false;
    if (gpio_reset_pin(GPIO_SD_D0) != ESP_OK) return false;
    if (gpio_set_direction(GPIO_SD_D0, GPIO_MODE_INPUT) != ESP_OK) return false;
    if (gpio_reset_pin(GPIO_LED_DATA) != ESP_OK) return false;
    if (gpio_set_direction(GPIO_LED_DATA, GPIO_MODE_INPUT) != ESP_OK) return false;

    if (gpio_get_level(GPIO_SD_PWR)) {
        *rc = 0x01;
        return false;
    }  // Check that power enable is pulled low

    if (gpio_set_direction(GPIO_SD_PWR, GPIO_MODE_OUTPUT) != ESP_OK) return false;
    if (gpio_set_level(GPIO_SD_PWR, 1) != ESP_OK) return false;

    vTaskDelay(10 / portTICK_PERIOD_MS);

    // SD pins should be pulled high
    if (!gpio_get_level(GPIO_SD_CMD)) {
        *rc = 0x02;
        return false;
    }
    if (!gpio_get_level(GPIO_SD_CLK)) {
        *rc = 0x04;
        return false;
    }
    if (!gpio_get_level(GPIO_SD_D0)) {
        *rc = 0x08;
        return false;
    }

    return true;
}

bool run_basic_tests() {
    pax_buf_t*        pax_buffer = get_pax_buffer();
    const pax_font_t* font;
    int               line = 0;
    bool              ok   = true;

    /* Screen init */
    font = pax_font_sky_mono;

    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0x8060f0);
    display_flush();

    /* Run mandatory tests */
    RUN_TEST_MANDATORY("RP2040", test_rp2040_init);
    RUN_TEST_MANDATORY("ICE40", test_ice40_init);
    RUN_TEST_MANDATORY("BNO055", test_bno055_init);
    RUN_TEST_MANDATORY("BME680", test_bme680_init);

    /* Run tests */
    RUN_TEST("STUCK BUTTONS", test_stuck_buttons);
    RUN_TEST("SD/LED POWER", test_sd_power);
    RUN_TEST("Battery voltage", test_adc_vbat);
    RUN_TEST("USB voltage", test_adc_vusb);

error:
    /* Fail result on screen */
    if (!ok) pax_draw_text(pax_buffer, 0xffff0000, font, 36, 0, 20 * line, "FAIL");
    display_flush();
    return ok;
}

const uint8_t led_green[15] = {50, 0, 0, 50, 0, 0, 50, 0, 0, 50, 0, 0, 50, 0, 0};
const uint8_t led_red[15]   = {0, 50, 0, 0, 50, 0, 0, 50, 0, 0, 50, 0, 0, 50, 0};
const uint8_t led_blue[15]  = {0, 0, 50, 0, 0, 50, 0, 0, 50, 0, 0, 50, 0, 0, 50};

void factory_test() {
    pax_buf_t* pax_buffer        = get_pax_buffer();
    uint8_t    factory_test_done = nvs_get_u8_default("system", "factory_test", 0);
    if (!factory_test_done) {
        bool result;

        ESP_LOGI(TAG, "Factory test start");

        result = run_basic_tests();

        gpio_set_direction(GPIO_SD_PWR, GPIO_MODE_OUTPUT);
        gpio_set_level(GPIO_SD_PWR, 1);
        ws2812_init(GPIO_LED_DATA);
        if (result) {
            ws2812_send_data(led_blue, sizeof(led_blue));
        } else {
            ws2812_send_data(led_red, sizeof(led_red));
        }

        if (!result) goto test_end;

        RP2040* rp2040 = get_rp2040();

        result = run_fpga_tests(rp2040->queue);
        if (!result) {
            ws2812_send_data(led_red, sizeof(led_red));
            goto test_end;
        }

    // Wait for the operator to unplug the badge
    test_end:

        if (result) {
            esp_err_t res = nvs_set_u8_fixed("system", "factory_test", 0x01);
            if (res != ESP_OK) {
                ESP_LOGE(TAG, "Failed to store test result %d\n", res);
                result = false;
                ws2812_send_data(led_red, sizeof(led_red));
                pax_noclip(pax_buffer);
                pax_background(pax_buffer, 0xa85a32);
                display_flush();
            }
            nvs_set_u8_fixed("system", "force_sponsors", 0x01);  // Force showing sponsors on first boot
            wifi_set_defaults();
            pax_noclip(pax_buffer);
            pax_background(pax_buffer, 0x00FF00);
            display_flush();
            ws2812_send_data(led_green, sizeof(led_green));
        }

        while (true) {
            if (result) play_bootsound();
            vTaskDelay(3000 / portTICK_PERIOD_MS);
        }
    }
}
