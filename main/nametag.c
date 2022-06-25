#include "nametag.h"

#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "hardware.h"
#include "ili9341.h"
#include "nvs.h"
#include "pax_gfx.h"
#include "rp2040.h"
#include "wifi_connect.h"

#define SLEEP_DELAY 10000
static const char *TAG = "nametag";

static void place_in_sleep(xQueueHandle buttonQueue, pax_buf_t *pax_buffer, ILI9341 *ili9341);
static void show_name(xQueueHandle buttonQueue, pax_buf_t *pax_buffer, ILI9341 *ili9341, const char *name);

// Shows the name tag.
// Will fall into deep sleep if left alone for long enough.
void show_nametag(xQueueHandle buttonQueue, pax_buf_t *pax_buffer, ILI9341 *ili9341) {
    // Open NVS.
    nvs_handle_t handle;
    esp_err_t    res = nvs_open("owner", NVS_READWRITE, &handle);

    // Read nickname.
    size_t required = 0;
    res             = nvs_get_str(handle, "nickname", NULL, &required);
    char *buffer;
    if (res) {
        ESP_LOGE(TAG, "Error reading nickname: %s", esp_err_to_name(res));
        buffer = strdup("Fancy Name!");
    } else {
        buffer           = malloc(required + 1);
        buffer[required] = 0;
        res              = nvs_get_str(handle, "nickname", buffer, &required);
        if (res) {
            *buffer = 0;
        }
    }

    // Schedule sleep time.
    uint64_t sleep_time = esp_timer_get_time() / 1000 + SLEEP_DELAY;
    ESP_LOGI(TAG, "Scheduled sleep in %d millis", SLEEP_DELAY);
    rp2040_input_message_t msg;
    while (1) {
        // Display the name.
        show_name(buttonQueue, pax_buffer, ili9341, buffer);
        // Await buttons.
        if (xQueueReceive(buttonQueue, &msg, pdMS_TO_TICKS(SLEEP_DELAY + 10))) {
            // Check for go back buttons.
            if (msg.input == RP2040_INPUT_BUTTON_HOME && msg.state) {
                goto exit;
            } else if (msg.input == RP2040_INPUT_BUTTON_BACK && msg.state) {
                goto exit;
            }
            // Reschedule sleep time.
            sleep_time = esp_timer_get_time() / 1000 + SLEEP_DELAY;
            ESP_LOGI(TAG, "Recheduled sleep in %d millis", SLEEP_DELAY);
        }
        if (esp_timer_get_time() / 1000 > sleep_time) {
            // Time to enter sleep mode.
            place_in_sleep(buttonQueue, pax_buffer, ili9341);
            goto exit;
        }
    }

exit:
    free(buffer);
    nvs_close(handle);
}

// Show them names.
static void show_name(xQueueHandle buttonQueue, pax_buf_t *pax_buffer, ILI9341 *ili9341, const char *name) {
    const pax_font_t *font = pax_get_font("saira condensed");

    // Set name scale.
    float      scale = font->default_size;
    pax_vec1_t dims  = pax_text_size(font, scale, name);
    if (dims.x > pax_buffer->width) {
        scale *= pax_buffer->width / dims.x;
        dims = pax_text_size(font, scale, name);
    }

    // Center vertically.
    pax_background(pax_buffer, 0);
    pax_center_text(pax_buffer, -1, font, scale, pax_buffer->width / 2, (pax_buffer->height - dims.y) / 2, name);
    ili9341_write(ili9341, pax_buffer->buf);
}

// TODO: Place the ESP32 in sleep with button wakeup.
static void place_in_sleep(xQueueHandle buttonQueue, pax_buf_t *pax_buffer, ILI9341 *ili9341) {
    ESP_LOGW(TAG, "About to enter sleep!");

    // Notify the user of the slumber.
    const pax_font_t *font = pax_get_font("saira regular");
    pax_draw_text(pax_buffer, -1, font, font->default_size, 5, pax_buffer->height - 5 - font->default_size, "Sleeping...");
    ili9341_write(ili9341, pax_buffer->buf);

    // TODO: Power off peripherals to conserve power.

#if SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
    // Deep sleep if we can wake from it.
    // Set wakeup pins.
    uint64_t mask = 1 << GPIO_INT_RP2040;
    esp_deep_sleep_enable_gpio_wakeup(mask, ESP_GPIO_WAKEUP_GPIO_LOW);
    // Fall asleep.
    ESP_LOGW(TAG, "Entering deep sleep now!");
    fflush(stdout);
    fflush(stderr);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_deep_sleep_start();

#else
    // Light sleep because we can't wake from deep sleep.
    // Disable WiFi.
    wifi_disconnect_and_disable();
    // Set wakeup sources.
    esp_sleep_disable_wifi_wakeup();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    // Set wakeup pins.
    gpio_wakeup_enable(GPIO_INT_RP2040, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    // Fall alseep.
    ESP_LOGW(TAG, "Entering light sleep now!");
    fflush(stdout);
    fflush(stderr);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_light_sleep_start();
    // Consume the button event, if any.
    rp2040_input_message_t msg;
    xQueueReceive(buttonQueue, &msg, pdMS_TO_TICKS(10));

#endif
}
