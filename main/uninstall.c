#include "uninstall.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "appfs.h"
#include "appfs_wrapper.h"
#include "bootscreen.h"
#include "hardware.h"
#include "ili9341.h"
#include "menu.h"
#include "pax_gfx.h"
#include "rp2040.h"
#include "system_wrapper.h"

static const char* TAG = "uninstaller";

typedef struct _uninstall_menu_args {
    appfs_handle_t fd;
    char           name[512];
} uninstall_menu_args_t;

void uninstall_browser(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    menu_t*           menu = menu_alloc("Uninstall application", 20, 18);
    const pax_font_t* font = pax_get_font("saira regular");

    appfs_handle_t appfs_fd = APPFS_INVALID_FD;
    while (1) {
        appfs_fd = appfsNextEntry(appfs_fd);
        if (appfs_fd == APPFS_INVALID_FD) break;
        const char* name = NULL;
        appfsEntryInfo(appfs_fd, &name, NULL);
        uninstall_menu_args_t* args = malloc(sizeof(uninstall_menu_args_t));
        if (args == NULL) {
            ESP_LOGE(TAG, "Failed to malloc() menu args");
            return;
        }
        args->fd = appfs_fd;
        sprintf(args->name, name);
        menu_insert_item(menu, name, NULL, (void*) args, -1);
    }

    bool                   render   = true;
    uninstall_menu_args_t* menuArgs = NULL;

    pax_background(pax_buffer, 0xFFFFFF);
    pax_noclip(pax_buffer);
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "[A] uninstall app  [B] back");

    bool quit = false;

    while (1) {
        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(buttonQueue, &buttonMessage, 16 / portTICK_PERIOD_MS) == pdTRUE) {
            uint8_t pin   = buttonMessage.input;
            bool    value = buttonMessage.state;
            switch (pin) {
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
                        quit = true;
                    }
                    break;
                case RP2040_INPUT_BUTTON_ACCEPT:
                case RP2040_INPUT_JOYSTICK_PRESS:
                case RP2040_INPUT_BUTTON_SELECT:
                case RP2040_INPUT_BUTTON_START:
                    if (value) {
                        menuArgs = menu_get_callback_args(menu, menu_get_position(menu));
                    }
                    break;
                default:
                    break;
            }
        }

        if (render) {
            menu_render(pax_buffer, menu, 0, 0, 320, 220, 0xFF72008a);
            ili9341_write(ili9341, pax_buffer->buf);
            render = false;
        }

        if (menuArgs != NULL) {
            char message[1024];
            sprintf(message, "Uninstalling %s...", menuArgs->name);
            printf("%s\n", message);
            display_boot_screen(pax_buffer, ili9341, message);
            appfsDeleteFile(menuArgs->name);
            menuArgs = NULL;
            break;
        }

        if (quit) {
            break;
        }
    }

    for (size_t index = 0; index < menu_get_length(menu); index++) {
        free(menu_get_callback_args(menu, index));
    }

    menu_free(menu);
}
