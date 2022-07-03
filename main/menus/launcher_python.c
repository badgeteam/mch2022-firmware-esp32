#include "launcher_python.h"

#include <cJSON.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_vfs.h>
#include <esp_vfs_fat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "appfs.h"
#include "appfs_wrapper.h"
#include "bootscreen.h"
#include "graphics_wrapper.h"
#include "hardware.h"
#include "ili9341.h"
#include "menu.h"
#include "metadata.h"
#include "pax_codecs.h"
#include "pax_gfx.h"
#include "rp2040.h"
#include "rtc_memory.h"
#include "system_wrapper.h"

extern const uint8_t python_png_start[] asm("_binary_python_png_start");
extern const uint8_t python_png_end[] asm("_binary_python_png_end");

extern const uint8_t hatchery_png_start[] asm("_binary_hatchery_png_start");
extern const uint8_t hatchery_png_end[] asm("_binary_hatchery_png_end");

static appfs_handle_t python_appfs_fd = APPFS_INVALID_FD;

typedef enum action { ACTION_NONE, ACTION_TEST } menu_python_action_t;

static void start_python_app(const char* path) {
    rtc_memory_string_write(path);
    appfs_boot_app(python_appfs_fd);
}

static bool populate_menu(menu_t* menu) {
    bool internal_result = populate_menu_from_path(menu, "/internal/apps/python", (void*) python_png_start, python_png_end - python_png_start);
    bool sdcard_result   = populate_menu_from_path(menu, "/sd/apps/python", (void*) python_png_start, python_png_end - python_png_start);
    return internal_result | sdcard_result;
}

void menu_launcher_python(xQueueHandle button_queue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    python_appfs_fd           = appfsOpen("python");
    bool python_not_installed = (python_appfs_fd == APPFS_INVALID_FD);

    const char* title = "BadgePython apps";
    menu_t*     menu  = menu_alloc(title, 34, 18);

    menu->fgColor           = 0xFF000000;
    menu->bgColor           = 0xFFFFFFFF;
    menu->bgTextColor       = 0xFF000000;
    menu->selectedItemColor = 0xFFfec859;
    menu->borderColor       = 0xFFfa448c;
    menu->titleColor        = 0xFFfec859;
    menu->titleBgColor      = 0xFFfa448c;
    menu->scrollbarBgColor  = 0xFFCCCCCC;
    menu->scrollbarFgColor  = 0xFF555555;

    pax_buf_t icon_python;
    pax_decode_png_buf(&icon_python, (void*) python_png_start, python_png_end - python_png_start, PAX_BUF_32_8888ARGB, 0);
    menu_set_icon(menu, &icon_python);

    pax_buf_t icon_hatchery;
    pax_decode_png_buf(&icon_hatchery, (void*) hatchery_png_start, hatchery_png_end - hatchery_png_start, PAX_BUF_32_8888ARGB, 0);

    if (!python_not_installed) {
        populate_menu(menu);
        menu_insert_item_icon(menu, "Python Hatchery", NULL, (void*) strdup("dashboard.installer"), -1, &icon_hatchery);
    }

    char* app_to_start = NULL;
    bool  render       = true;
    bool  render_help  = true;
    bool  quit         = false;
    while (!quit) {
        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(button_queue, &buttonMessage, 16 / portTICK_PERIOD_MS) == pdTRUE) {
            if (buttonMessage.state) {
                switch (buttonMessage.input) {
                    case RP2040_INPUT_JOYSTICK_DOWN:
                        if (!python_not_installed) {
                            menu_navigate_next(menu);
                        }
                        render = true;
                        break;
                    case RP2040_INPUT_JOYSTICK_UP:
                        if (!python_not_installed) {
                            menu_navigate_previous(menu);
                        }
                        render = true;
                        break;
                    case RP2040_INPUT_BUTTON_HOME:
                    case RP2040_INPUT_BUTTON_BACK:
                        quit = true;
                        break;
                    case RP2040_INPUT_BUTTON_ACCEPT:
                    case RP2040_INPUT_JOYSTICK_PRESS:
                    case RP2040_INPUT_BUTTON_SELECT:
                    case RP2040_INPUT_BUTTON_START:
                        if (!python_not_installed) {
                            app_to_start = (char*) menu_get_callback_args(menu, menu_get_position(menu));
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        if (render_help) {
            const pax_font_t* font = pax_font_saira_regular;
            pax_background(pax_buffer, 0xFFFFFF);
            pax_noclip(pax_buffer);
            if (!python_not_installed) {
                pax_draw_text(pax_buffer, 0xFFfa448c, font, 18, 5, 240 - 18, "🅰 start app 🅱 back");
            } else {
                pax_draw_text(pax_buffer, 0xFFfa448c, font, 18, 5, 240 - 18, "🅱 back");
            }
            render_help = false;
        }

        if (render) {
            if (!python_not_installed) {
                menu_render(pax_buffer, menu, 0, 0, 320, 220);
            } else {
                render_header(pax_buffer, 0, 0, pax_buffer->width, 34, 18, menu->titleColor, menu->titleBgColor, &icon_python, title);
                render_message(pax_buffer, "BadgePython is not installed\n\nPlease install BadgePython\nusing the Hatchery.");
            }
            ili9341_write(ili9341, pax_buffer->buf);
            render = false;
        }

        if (app_to_start != NULL) {
            display_boot_screen(pax_buffer, ili9341, "Starting app...");
            start_python_app(app_to_start);
            app_to_start = NULL;
            render       = true;
            render_help  = true;
        }
    }

    for (size_t index = 0; index < menu_get_length(menu); index++) {
        pax_buf_t* icon = menu_get_icon(menu, index);
        if (icon != NULL) pax_buf_destroy(icon);
        free(menu_get_callback_args(menu, index));
    }

    menu_free(menu);
    pax_buf_destroy(&icon_python);
    pax_buf_destroy(&icon_hatchery);
}
