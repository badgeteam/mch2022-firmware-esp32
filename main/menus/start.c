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
#include "launcher.h"
#include "settings.h"
#include "dev.h"

typedef enum action {
    ACTION_NONE,
    ACTION_APPS,
    ACTION_DEV,
    ACTION_SETTINGS
} menu_start_action_t;

void render_start_help(pax_buf_t* pax_buffer) {
    pax_background(pax_buffer, 0xFFFFFF);
    pax_noclip(pax_buffer);
    pax_draw_text(pax_buffer, 0xFF000000, NULL, 18, 5, 240 - 19, "[A] accept");
}

void menu_start(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    menu_t* menu = menu_alloc("Main menu");
    menu_insert_item(menu, "Apps", NULL, (void*) ACTION_APPS, -1);
    menu_insert_item(menu, "Development tools", NULL, (void*) ACTION_DEV, -1);
    menu_insert_item(menu, "Settings", NULL, (void*) ACTION_SETTINGS, -1);
    

    bool render = true;
    menu_start_action_t action = ACTION_NONE;
    
    render_start_help(pax_buffer);

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
                case RP2040_INPUT_BUTTON_ACCEPT:
                case RP2040_INPUT_JOYSTICK_PRESS:
                case RP2040_INPUT_BUTTON_SELECT:
                case RP2040_INPUT_BUTTON_START:
                    if (value) {
                        action = (menu_start_action_t) menu_get_callback_args(menu, menu_get_position(menu));
                    }
                    break;
                default:
                    break;
            }
        }

        if (render) {
            menu_render(pax_buffer, menu, 0, 0, 320, 220, 0xFF000000);
            ili9341_write(ili9341, pax_buffer->buf);
            render = false;
        }

        if (action != ACTION_NONE) {
            if (action == ACTION_APPS) {
                menu_launcher(buttonQueue, pax_buffer, ili9341);
            } else if (action == ACTION_SETTINGS) {
                menu_settings(buttonQueue, pax_buffer, ili9341);
            } else if (action == ACTION_DEV) {
                menu_dev(buttonQueue, pax_buffer, ili9341);
            }
            action = ACTION_NONE;
            render = true;
            render_start_help(pax_buffer);
        }
    }

    menu_free(menu);
}
