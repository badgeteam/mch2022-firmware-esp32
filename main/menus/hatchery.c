#include <stdio.h>
#include <string.h>
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_system.h>
#include <esp_err.h>
#include <esp_log.h>
#include "ili9341.h"
#include "pax_gfx.h"
#include "pax_codecs.h"
#include "menu.h"
#include "rp2040.h"

extern const uint8_t apps_png_start[] asm("_binary_apps_png_start");
extern const uint8_t apps_png_end[] asm("_binary_apps_png_end");

typedef struct {
    //appfs_handle_t fd;
    //menu_launcher_action_t action;
    void *data;
} menu_hatchery_args_t;

typedef void (*fill_menu_items_fn_t)(menu_t *menu);
typedef void (*action_fn_t)(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, menu_hatchery_args_t *args);

static void menu_generic(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, const char *select, fill_menu_items_fn_t fill_menu_items, action_fn_t action);
static void add_menu_item(menu_t *menu, const char *name, menu_hatchery_args_t *args);


static void fill_menu_items_apps(menu_t *menu) {
    add_menu_item(menu, "App A", NULL);
    add_menu_item(menu, "Test App", NULL);
    add_menu_item(menu, "App xx", NULL);
}

static void action_apps(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, menu_hatchery_args_t *args) {
}


static void fill_menu_items_categories(menu_t *menu) {
    add_menu_item(menu, "Fun", NULL);
    add_menu_item(menu, "Test", NULL);
    add_menu_item(menu, "Strange", NULL);
}

static void action_categories(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, menu_hatchery_args_t *args) {
    menu_generic(buttonQueue, pax_buffer, ili9341, "[A] select app  [B] back", fill_menu_items_apps, action_apps);
}


static void fill_menu_items_types(menu_t *menu) {
    add_menu_item(menu, "App", NULL);
    add_menu_item(menu, "Python", NULL);
    add_menu_item(menu, "FPGA", NULL);
}

static void action_types(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, menu_hatchery_args_t *args) {
    menu_generic(buttonQueue, pax_buffer, ili9341, "[A] select category  [B] back", fill_menu_items_categories, action_categories);
}


void menu_hatchery(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    menu_generic(buttonQueue, pax_buffer, ili9341, "[A] select type  [B] back", fill_menu_items_types, action_types);
}

// Generic functions

static void add_menu_item(menu_t *menu, const char *name, menu_hatchery_args_t *args) {
    args = malloc(sizeof(menu_hatchery_args_t));
    args->data = 0;
    menu_insert_item(menu, name, NULL, (void*) args, -1);
}

static void menu_generic(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, const char *select, fill_menu_items_fn_t fill_menu_items, action_fn_t action) {
    menu_t* menu = menu_alloc("Hatchery", 34, 18);
    
    menu->fgColor           = 0xFF000000;
    menu->bgColor           = 0xFFFFFFFF;
    menu->bgTextColor       = 0xFFFFFFFF;
    menu->selectedItemColor = 0xFFfa448c;
    menu->borderColor       = 0xFF491d88;
    menu->titleColor        = 0xFFfa448c;
    menu->titleBgColor      = 0xFF491d88;
    menu->scrollbarBgColor  = 0xFFCCCCCC;
    menu->scrollbarFgColor  = 0xFF555555;
     
    pax_buf_t icon_apps;
    pax_decode_png_buf(&icon_apps, (void*) apps_png_start, apps_png_end - apps_png_start, PAX_BUF_32_8888ARGB, 0);
    
    menu_set_icon(menu, &icon_apps);

    const pax_font_t *font = pax_get_font("saira regular");

    fill_menu_items(menu);
    
    bool render = true;
    menu_hatchery_args_t* menuArgs = NULL;
    
    pax_background(pax_buffer, 0xFFFFFF);
    pax_noclip(pax_buffer);
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, select);

    bool quit = false;

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

        if (quit) {
            break;
        }

        if (menuArgs != NULL) {
            action(buttonQueue, pax_buffer, ili9341, menuArgs);
            menuArgs = NULL;
            render = true;
        }

        if (render) {
            menu_render(pax_buffer, menu, 0, 0, 320, 220, 0xFF491d88);
            ili9341_write(ili9341, pax_buffer->buf);
            render = false;
        }
    }

    for (size_t index = 0; index < menu_get_length(menu); index++) {
        free(menu_get_callback_args(menu, index));
    }

    menu_free(menu);
    pax_buf_destroy(&icon_apps);
}
