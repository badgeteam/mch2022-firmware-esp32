#include <stdio.h>
#include <string.h>
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_system.h>
#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "appfs.h"
#include "ili9341.h"
#include "pax_gfx.h"
#include "menu.h"
#include "rp2040.h"
#include "appfs_wrapper.h"
#include "hardware.h"
#include "system_wrapper.h"
#include "bootscreen.h"
#include "wifi_connect.h"
#include "wifi_ota.h"
#include "graphics_wrapper.h"

static const char *TAG = "wifi menu";

typedef enum action {
    ACTION_NONE,
    ACTION_BACK,
    ACTION_SHOW,
    ACTION_SCAN,
    ACTION_MANUAL
} menu_wifi_action_t;

void render_wifi_help(pax_buf_t* pax_buffer) {
    pax_background(pax_buffer, 0xFFFFFF);
    pax_noclip(pax_buffer);
    pax_draw_text(pax_buffer, 0xFF000000, NULL, 18, 5, 240 - 19, "[A] accept  [B] back");
}

void wifi_show(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341);
void wifi_setup(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, bool scan);

void menu_wifi(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    menu_t* menu = menu_alloc("WiFi configuration");
    menu_insert_item(menu, "Show current settings", NULL, (void*) ACTION_SHOW, -1);
    menu_insert_item(menu, "Scan for networks", NULL, (void*) ACTION_SCAN, -1);
    menu_insert_item(menu, "Configure manually", NULL, (void*) ACTION_MANUAL, -1);

    bool render = true;
    menu_wifi_action_t action = ACTION_NONE;

    render_wifi_help(pax_buffer);

    while (1) {
        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(buttonQueue, &buttonMessage, 16 / portTICK_PERIOD_MS) == pdTRUE) {
            uint8_t pin = buttonMessage.input;
            bool value = buttonMessage.state;
            switch(pin) {
                case RP2040_INPUT_JOYSTICK_DOWN:
                    if (value) {
                        menu_navigate_next(menu);
                        render = true;
                    }
                    break;
                case RP2040_INPUT_JOYSTICK_UP:
                    if (value) {
                        menu_navigate_previous(menu);
                        render = true;
                    }
                    break;
                case RP2040_INPUT_BUTTON_HOME:
                case RP2040_INPUT_BUTTON_BACK:
                    if (value) {
                        action = ACTION_BACK;
                    }
                    break;
                case RP2040_INPUT_BUTTON_ACCEPT:
                case RP2040_INPUT_JOYSTICK_PRESS:
                case RP2040_INPUT_BUTTON_SELECT:
                case RP2040_INPUT_BUTTON_START:
                    if (value) {
                        action = (menu_wifi_action_t) menu_get_callback_args(menu, menu_get_position(menu));
                    }
                    break;
                default:
                    break;
            }
        }

        if (render) {
            menu_render(pax_buffer, menu, 0, 0, 320, 220, 0xFF2f55a8);
            ili9341_write(ili9341, pax_buffer->buf);
            render = false;
        }

        if (action != ACTION_NONE) {
            if (action == ACTION_SHOW) {
                wifi_show(buttonQueue, pax_buffer, ili9341);
            } else if (action == ACTION_SCAN) {
                wifi_setup(buttonQueue, pax_buffer, ili9341, true);
            } else if (action == ACTION_MANUAL) {
                wifi_setup(buttonQueue, pax_buffer, ili9341, false);
            } else if (action == ACTION_BACK) {
                break;
            }
            render = true;
            action = ACTION_NONE;
            render_wifi_help(pax_buffer);
        }
    }

    menu_free(menu);
}

void wifi_show(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    nvs_handle_t handle;

    nvs_open("system", NVS_READWRITE, &handle);
    char ssid[33] = "<not set>";
    char password[33] = "<not set>";
    size_t requiredSize;

    esp_err_t res = nvs_get_str(handle, "wifi.ssid", NULL, &requiredSize);
    if ((res == ESP_OK) && (requiredSize < sizeof(ssid))) {
        res = nvs_get_str(handle, "wifi.ssid", ssid, &requiredSize);
    }

    res = nvs_get_str(handle, "wifi.password", NULL, &requiredSize);
    if ((res == ESP_OK) && (requiredSize < sizeof(password))) {
        res = nvs_get_str(handle, "wifi.password", password, &requiredSize);
    }

    nvs_close(handle);

    char buffer[300];
    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0xFFFFFF);
    snprintf(buffer, sizeof(buffer), "SSID is %s", ssid);
    pax_draw_text(pax_buffer, 0xFF000000, NULL, 18, 0, 20*0, buffer);
    snprintf(buffer, sizeof(buffer), "Password is %s", password);
    pax_draw_text(pax_buffer, 0xFF000000, NULL, 18, 0, 20*1, buffer);
    ili9341_write(ili9341, pax_buffer->buf);

    bool quit = false;
    while (!quit) {
        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(buttonQueue, &buttonMessage, 16 / portTICK_PERIOD_MS) == pdTRUE) {
            uint8_t pin = buttonMessage.input;
            bool value = buttonMessage.state;
            switch(pin) {
                case RP2040_INPUT_BUTTON_HOME:
                case RP2040_INPUT_BUTTON_BACK:
                case RP2040_INPUT_BUTTON_ACCEPT:
                case RP2040_INPUT_JOYSTICK_PRESS:
                case RP2040_INPUT_BUTTON_SELECT:
                case RP2040_INPUT_BUTTON_START:
                    if (value) quit = true;
                    break;
                default:
                    break;
            }
        }
    }
}

void wifi_setup(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, bool scan) {
    nvs_handle_t handle;
    nvs_open("system", NVS_READWRITE, &handle);
    char ssid[33];
    char password[33];
    size_t requiredSize;
    esp_err_t res = nvs_get_str(handle, "wifi.ssid", NULL, &requiredSize);
    if (res != ESP_OK) {
        strcpy(ssid, "");
        strcpy(password, "");
    } else if (requiredSize < sizeof(ssid)) {
        res = nvs_get_str(handle, "wifi.ssid", ssid, &requiredSize);
        if (res != ESP_OK) strcpy(ssid, "");
        res = nvs_get_str(handle, "wifi.password", NULL, &requiredSize);
        if (res != ESP_OK) {
            strcpy(password, "");
        } else if (requiredSize < sizeof(password)) {
            res = nvs_get_str(handle, "wifi.password", password, &requiredSize);
            if (res != ESP_OK) strcpy(password, "");
        }
    }
    bool accepted = keyboard(buttonQueue, pax_buffer, ili9341, 30, 30, pax_buffer->width - 60, pax_buffer->height - 60, "WiFi SSID", "Press HOME to exit", ssid, sizeof(ssid));
    if (accepted) {
        accepted = keyboard(buttonQueue, pax_buffer, ili9341, 30, 30, pax_buffer->width - 60, pax_buffer->height - 60, "WiFi password", "Press HOME to exit", password, sizeof(password));
    }
    if (accepted) {
        nvs_set_str(handle, "wifi.ssid", ssid);
        nvs_set_str(handle, "wifi.password", password);
        display_boot_screen(pax_buffer, ili9341, "WiFi settings stored");
    }
    nvs_set_u8(handle, "wifi.use_ent", 0);
    nvs_close(handle);
}

