#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "appfs_wrapper.h"
#include "hatchery_client.h"
#include "ili9341.h"
#include "menu.h"
#include "pax_codecs.h"
#include "pax_gfx.h"
#include "rp2040.h"
#include "bootscreen.h"

static const char *TAG = "HatchMenu";

extern const uint8_t apps_png_start[] asm("_binary_apps_png_start");
extern const uint8_t apps_png_end[] asm("_binary_apps_png_end");

// Store app to appfs/fat

static void store_app(xQueueHandle buttonQueue, pax_buf_t *pax_buffer, ILI9341 *ili9341, hatchery_app_t *app) {
    hatchery_query_file(app, app->files);

    if (app->files->contents != NULL) {
        esp_err_t res = appfs_store_in_memory_app(buttonQueue, pax_buffer, ili9341, app->slug, app->name, app->version, app->files->size, app->files->contents);
    }

    free(app->files->contents);
    app->files->contents = NULL;
}

// Menus

typedef void (*fill_menu_items_fn_t)(menu_t *menu, void *context);
typedef void (*action_fn_t)(xQueueHandle buttonQueue, pax_buf_t *pax_buffer, ILI9341 *ili9341, void *args);

static void menu_generic(xQueueHandle buttonQueue, pax_buf_t *pax_buffer, ILI9341 *ili9341, const char *select, fill_menu_items_fn_t fill_menu_items,
                         action_fn_t action, void *context);
static void add_menu_item(menu_t *menu, const char *name, void *callback_args);

// Apps menu

static void fill_menu_items_apps(menu_t *menu, void *context) {
    hatchery_category_t *category = (hatchery_category_t *) context;

    hatchery_query_apps(category);
    for (hatchery_app_t *apps = category->apps; apps != NULL; apps = apps->next) {
        add_menu_item(menu, apps->name, apps);
    }
}

static void action_apps(xQueueHandle buttonQueue, pax_buf_t *pax_buffer, ILI9341 *ili9341, void *args) {
    hatchery_app_t *app = (hatchery_app_t *) args;

    if (app->files == NULL) {
        hatchery_query_app(app);
    }

    const pax_font_t *font = pax_get_font("saira regular");

    float entry_height = 34;
    float text_height  = 18;
    float text_offset  = ((entry_height - text_height) / 2) + 1;

    pax_buf_t icon_apps;
    pax_decode_png_buf(&icon_apps, (void *) apps_png_start, apps_png_end - apps_png_start, PAX_BUF_32_8888ARGB, 0);

    // Show information about app and give user option to install app
    pax_background(pax_buffer, 0xFFFFFF);

    pax_noclip(pax_buffer);
    pax_draw_text(pax_buffer, 0xFF000000, font, text_height, 5, 240 - 18, "🅰 Install 🅱 Back");

    pax_simple_rect(pax_buffer, 0xFF491d88, 0, 0, 320, entry_height);
    pax_clip(pax_buffer, 1, text_offset, 320 - 2, text_height);
    pax_draw_text(pax_buffer, 0xFFfa448c, font, text_height, icon_apps.width + 1, text_offset, "Hatchery");
    pax_noclip(pax_buffer);
    pax_draw_image(pax_buffer, &icon_apps, 0, 0);
    pax_noclip(pax_buffer);
    pax_draw_text(pax_buffer, 0xFF000000, font, text_height, 1, 40, app->name);
    pax_draw_text(pax_buffer, 0xFF000000, font, text_height, 1, 60, app->author);
    pax_draw_text(pax_buffer, 0xFF000000, font, text_height, 1, 80, app->license);
    pax_draw_text(pax_buffer, 0xFF000000, font, text_height, 1, 100, app->description);
    char buffer[21];
    snprintf(buffer, 20, "version %d", 1);
    buffer[20] = '\0';
    pax_draw_text(pax_buffer, 0xFF000000, font, text_height, 1, 120, buffer);
    ili9341_write(ili9341, pax_buffer->buf);

    bool load_app = false;
    bool quit     = false;

    while (1) {
        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(buttonQueue, &buttonMessage, 16 / portTICK_PERIOD_MS) == pdTRUE) {
            uint8_t pin   = buttonMessage.input;
            bool    value = buttonMessage.state;
            switch (pin) {
                case RP2040_INPUT_JOYSTICK_DOWN:
                case RP2040_INPUT_JOYSTICK_UP:
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
                        load_app = true;
                    }
                    break;
                default:
                    break;
            }
        }

        if (load_app) {
            if (app->files == NULL) {
                return;
            }
            display_busy(pax_buffer, ili9341);
            store_app(buttonQueue, pax_buffer, ili9341, app);

            break;
        }
        if (quit) {
            break;
        }
    }

    // pax_buf_destroy(&icon_apps);
}

// Category menu

static void fill_menu_items_categories(menu_t *menu, void *context) {
    hatchery_app_type_t *app_type = (hatchery_app_type_t *) context;

    hatchery_query_categories(app_type);
    for (hatchery_category_t *category = app_type->categories; category != NULL; category = category->next) {
        add_menu_item(menu, category->name, category);
    }
}

static void action_categories(xQueueHandle buttonQueue, pax_buf_t *pax_buffer, ILI9341 *ili9341, void *args) {
    menu_generic(buttonQueue, pax_buffer, ili9341, "🅰 select app  🅱 back", fill_menu_items_apps, action_apps, args);
}

// App types menu

static void fill_menu_items_types(menu_t *menu, void *context) {
    hatchery_server_t *server = (hatchery_server_t *) context;

    hatchery_query_app_types(server);
    for (hatchery_app_type_t *app_type = server->app_types; app_type != NULL; app_type = app_type->next) {
        add_menu_item(menu, app_type->name, app_type);
    }
}

static void action_types(xQueueHandle buttonQueue, pax_buf_t *pax_buffer, ILI9341 *ili9341, void *args) {
    menu_generic(buttonQueue, pax_buffer, ili9341, "🅰 select category  🅱 back", fill_menu_items_categories, action_categories, args);
}

// Main entry function

void menu_hatchery(xQueueHandle buttonQueue, pax_buf_t *pax_buffer, ILI9341 *ili9341) {
    hatchery_server_t server;
    server.url       = "https://mch2022.badge.team/v2/mch2022";
    server.app_types = NULL;
    menu_generic(buttonQueue, pax_buffer, ili9341, "🅰 select type  🅱 back", fill_menu_items_types, action_types, &server);
    hatchery_app_type_free(server.app_types);
}

// Generic functions

static void add_menu_item(menu_t *menu, const char *name, void *callback_args) {
    static int nothing;
    if (callback_args == NULL) {
        callback_args = &nothing;
    }
    menu_insert_item(menu, name, NULL, callback_args, -1);
}

static void menu_generic(xQueueHandle buttonQueue, pax_buf_t *pax_buffer, ILI9341 *ili9341, const char *select, fill_menu_items_fn_t fill_menu_items,
                         action_fn_t action, void *context) {
    menu_t *menu = menu_alloc("Hatchery", 34, 18);

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
    pax_decode_png_buf(&icon_apps, (void *) apps_png_start, apps_png_end - apps_png_start, PAX_BUF_32_8888ARGB, 0);

    menu_set_icon(menu, &icon_apps);

    const pax_font_t *font = pax_get_font("saira regular");

    fill_menu_items(menu, context);

    bool  render   = true;
    void *menuArgs = NULL;

    pax_background(pax_buffer, 0xFFFFFF);
    pax_noclip(pax_buffer);
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, select);

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

        if (quit) {
            break;
        }

        if (menuArgs != NULL) {
            display_busy(pax_buffer, ili9341);
            action(buttonQueue, pax_buffer, ili9341, menuArgs);
            menuArgs = NULL;
            render   = true;
        }

        if (render) {
            menu_render(pax_buffer, menu, 0, 0, 320, 220, 0xFF491d88);
            ili9341_write(ili9341, pax_buffer->buf);
            render = false;
        }
    }

    menu_free(menu);
    pax_buf_destroy(&icon_apps);
}
