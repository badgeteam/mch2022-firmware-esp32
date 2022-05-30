#include <stdio.h>
#include <string.h>
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_system.h>
#include <esp_err.h>
#include <esp_log.h>
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
#include "wifi.h"

typedef enum action {
    ACTION_NONE,
    ACTION_BACK,
    ACTION_WIFI,
    ACTION_OTA,
    ACTION_RP2040_BL,
    ACTION_UNINSTALL
} menu_settings_action_t;

void render_settings_help(pax_buf_t* pax_buffer) {
    pax_background(pax_buffer, 0xFFFFFF);
    pax_noclip(pax_buffer);
    pax_draw_text(pax_buffer, 0xFF000000, NULL, 18, 5, 240 - 19, "[A] accept  [B] back");
}

void menu_settings(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    menu_t* menu = menu_alloc("Settings");
    menu_insert_item(menu, "WiFi configuration", NULL, (void*) ACTION_WIFI, -1);
    menu_insert_item(menu, "Firmware update", NULL, (void*) ACTION_OTA, -1);
    menu_insert_item(menu, "Flash RP2040 firmware", NULL, (void*) ACTION_RP2040_BL, -1);
    menu_insert_item(menu, "Uninstall app", NULL, (void*) ACTION_UNINSTALL, -1);

    bool render = true;
    menu_settings_action_t action = ACTION_NONE;

    render_settings_help(pax_buffer);

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
                        action = (menu_settings_action_t) menu_get_callback_args(menu, menu_get_position(menu));
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
            if (action == ACTION_RP2040_BL) {
                display_boot_screen(pax_buffer, ili9341, "Please wait...");
                rp2040_reboot_to_bootloader(get_rp2040());
                esp_restart();
            } else if (action == ACTION_OTA) {
                display_boot_screen(pax_buffer, ili9341, "Connecting to WiFi...");
                if (wifi_connect_to_stored()) {
                    display_boot_screen(pax_buffer, ili9341, "Starting firmware update...");
                    ota_update();
                } else {
                    display_boot_screen(pax_buffer, ili9341, "Failed to connect to WiFi");
                    vTaskDelay(500 / portTICK_PERIOD_MS);
                }
            } else if (action == ACTION_WIFI) {
                menu_wifi(buttonQueue, pax_buffer, ili9341);
            } else if (action == ACTION_BACK) {
                break;
            }
            render = true;
            action = ACTION_NONE;
            render_settings_help(pax_buffer);
        }
    }

    menu_free(menu);
}
