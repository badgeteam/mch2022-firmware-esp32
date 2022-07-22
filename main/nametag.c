#include "nametag.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp32/ulp.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "graphics_wrapper.h"
#include "hardware.h"
#include "ili9341.h"
#include "nvs.h"
#include "pax_gfx.h"
#include "rp2040.h"
#include "sdkconfig.h"
#include "soc/rtc.h"
#include "wifi_connect.h"
#include "ws2812.h"

#define SLEEP_DELAY 10000
static const char *TAG = "nametag";

typedef enum { NICKNAME_THEME_HELLO = 0, NICKNAME_THEME_SIMPLE, NICKNAME_THEME_GAMER, NICKNAME_THEME_LAST } nickname_theme_t;

void edit_nickname(xQueueHandle button_queue, pax_buf_t *pax_buffer, ILI9341 *ili9341) {
    nvs_handle_t handle;
    esp_err_t    res = nvs_open("owner", NVS_READWRITE, &handle);
    if (res != ESP_OK) return;

    char nickname[128] = {0};

    size_t size = 0;
    res         = nvs_get_str(handle, "nickname", NULL, &size);
    if ((res == ESP_OK) && (size <= sizeof(nickname) - 1)) {
        res = nvs_get_str(handle, "nickname", nickname, &size);
        if (res != ESP_OK) {
            nickname[0] = '\0';
        }
    }

    bool accepted = keyboard(button_queue, pax_buffer, ili9341, 30, 30, pax_buffer->width - 60, pax_buffer->height - 60, "Nickname", "Press 🅷 to cancel",
                             nickname, sizeof(nickname) - 1);

    if (accepted) {
        nvs_set_str(handle, "nickname", nickname);
    }
    nvs_close(handle);
}

static void show_name(xQueueHandle button_queue, pax_buf_t *pax_buffer, ILI9341 *ili9341, const char *name, nickname_theme_t theme, bool instructions) {
    const pax_font_t *title_font        = pax_font_saira_condensed;
    const pax_font_t *instructions_font = pax_font_saira_regular;
    const pax_font_t *name_font         = ((theme == NICKNAME_THEME_HELLO) || (theme == NICKNAME_THEME_GAMER)) ? pax_font_marker : pax_font_saira_condensed;

    float      scale = (theme == NICKNAME_THEME_HELLO) ? 60 : name_font->default_size;
    pax_vec1_t dims  = pax_text_size(name_font, scale, name);
    if (dims.x > pax_buffer->width) {
        scale *= pax_buffer->width / dims.x;
        dims = pax_text_size(name_font, scale, name);
    }

    if (theme == NICKNAME_THEME_HELLO) {
        pax_background(pax_buffer, 0xFFFFFF);
        pax_simple_rect(pax_buffer, 0xFFFF0000, 0, 0, pax_buffer->width, 60);
        pax_simple_rect(pax_buffer, 0xFFFF0000, 0, pax_buffer->height - 20, pax_buffer->width, 20);
        pax_center_text(pax_buffer, 0xFFFFFFFF, title_font, 30, pax_buffer->width / 2, 2, "HELLO");
        pax_center_text(pax_buffer, 0xFFFFFFFF, title_font, 24, pax_buffer->width / 2, 30, "My name is:");
        pax_center_text(pax_buffer, 0xFF000000, name_font, scale, pax_buffer->width / 2, 60 + ((pax_buffer->height - 90) - dims.y) / 2, name);
    } else if (theme == NICKNAME_THEME_GAMER) {
        int hue = esp_random() & 255;
        pax_col_t color = pax_col_hsv(hue, 255 /*saturation*/, 255 /*brighness*/);
        pax_background(pax_buffer, 0xFFFFFF);
        pax_simple_rect(pax_buffer, color, 0, 0, pax_buffer->width, 60);
        pax_simple_rect(pax_buffer, color, 0, pax_buffer->height - 20, pax_buffer->width, 20);
        pax_center_text(pax_buffer, 0xFFFFFFFF, title_font, 30, pax_buffer->width / 2, 2, "HELLO");
        pax_center_text(pax_buffer, 0xFFFFFFFF, title_font, 24, pax_buffer->width / 2, 30, "My name is:");
        pax_center_text(pax_buffer, 0xFF000000, name_font, scale, pax_buffer->width / 2, 60 + ((pax_buffer->height - 90) - dims.y) / 2, name);
        
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >>  8) & 0xFF;
        uint8_t b = (color >>  0) & 0xFF;
        
        uint8_t led_buffer[50*3];
        for (uint8_t i = 0; i < sizeof(led_buffer); i+=3) {
            led_buffer[i] = g;
            led_buffer[i + 1] = r;
            led_buffer[i + 2] = b;
        }
        ws2812_send_data(led_buffer, sizeof(led_buffer));
    } else {
        pax_background(pax_buffer, 0x000000);
        pax_center_text(pax_buffer, 0xFFFFFFFF, name_font, scale, pax_buffer->width / 2, (pax_buffer->height - dims.y) / 2, name);
    }

    if (instructions) {
        pax_draw_text(pax_buffer, 0xFFFFFFFF, instructions_font, 14, 5, pax_buffer->height - 17, "🅱 exit 🅼 name 🅴 theme");
    }

    ili9341_write(ili9341, pax_buffer->buf);
}

static void place_in_sleep(xQueueHandle button_queue, pax_buf_t *pax_buffer, ILI9341 *ili9341) {
    esp_sleep_enable_ext0_wakeup(GPIO_INT_RP2040, false);
    ESP_LOGW(TAG, "Entering deep sleep now!");
    fflush(stdout);
    fflush(stderr);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_deep_sleep_start();
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

char *read_nickname() {
    char *buffer = NULL;

    nvs_handle_t handle;
    esp_err_t    res = nvs_open("owner", NVS_READWRITE, &handle);

    // Read nickname.
    size_t required = 0;
    res             = nvs_get_str(handle, "nickname", NULL, &required);
    if (res) {
        ESP_LOGE(TAG, "Error reading nickname: %s", esp_err_to_name(res));
        buffer = strdup("BADGE.TEAM");
    } else {
        buffer           = malloc(required + 1);
        buffer[required] = 0;
        res              = nvs_get_str(handle, "nickname", buffer, &required);
        if (res) {
            *buffer = 0;
        }
    }
    nvs_close(handle);

    return buffer;
}

static nickname_theme_t get_theme() {
    nvs_handle_t handle;
    if (nvs_open("owner", NVS_READWRITE, &handle) != ESP_OK) {
        return 0;
    }
    uint8_t result;
    if (nvs_get_u8(handle, "theme", &result) != ESP_OK) {
        result = 0;
    }
    nvs_close(handle);

    result = result % NICKNAME_THEME_LAST;
    return (nickname_theme_t) result;
}

static void set_theme(nickname_theme_t theme) {
    theme = theme % NICKNAME_THEME_LAST;
    nvs_handle_t handle;
    if (nvs_open("owner", NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_set_u8(handle, "theme", (uint8_t) theme);
    nvs_close(handle);
}

void show_nametag(xQueueHandle button_queue, pax_buf_t *pax_buffer, ILI9341 *ili9341) {
    nickname_theme_t theme      = get_theme();
    char            *buffer     = read_nickname();
    uint64_t         sleep_time = esp_timer_get_time() / 1000 + SLEEP_DELAY;
    if (theme != NICKNAME_THEME_GAMER) {
        ESP_LOGI(TAG, "Scheduled sleep in %d millis", SLEEP_DELAY);
    }
    rp2040_input_message_t msg;
    bool                   quit = false;
    while (!quit) {
        if ((esp_timer_get_time() / 1000 > sleep_time) && (theme != NICKNAME_THEME_GAMER)) {
            show_name(button_queue, pax_buffer, ili9341, buffer, theme, false);
            place_in_sleep(button_queue, pax_buffer, ili9341);
            break;
        }
        show_name(button_queue, pax_buffer, ili9341, buffer, theme, true);
        if (xQueueReceive(button_queue, &msg, pdMS_TO_TICKS(SLEEP_DELAY + 10))) {
            if (msg.state) {
                switch (msg.input) {
                    case RP2040_INPUT_BUTTON_BACK:
                    case RP2040_INPUT_BUTTON_HOME:
                        quit = true;
                        break;
                    case RP2040_INPUT_BUTTON_MENU:
                        edit_nickname(button_queue, pax_buffer, ili9341);
                        free(buffer);
                        buffer = read_nickname();
                        break;
                    case RP2040_INPUT_BUTTON_SELECT:
                        theme = (theme + 1) % NICKNAME_THEME_LAST;
                        set_theme(theme);
                        break;
                    default:
                        break;
                }
            }
            sleep_time = esp_timer_get_time() / 1000 + SLEEP_DELAY;
            ESP_LOGI(TAG, "Recheduled sleep in %d millis", SLEEP_DELAY);
        }
    }
    
    uint8_t led_buffer[50*3] = {0};
    ws2812_send_data(led_buffer, sizeof(led_buffer));

    free(buffer);
}
