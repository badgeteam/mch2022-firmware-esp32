#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "app_update.h"
#include "bootscreen.h"
#include "dev.h"
#include "hardware.h"
#include "hatchery.h"
#include "launcher.h"
#include "math.h"
#include "menu.h"
#include "nametag.h"
#include "pax_codecs.h"
#include "pax_gfx.h"
#include "rp2040.h"
#include "settings.h"
#include "wifi_ota.h"

extern const uint8_t home_png_start[] asm("_binary_home_png_start");
extern const uint8_t home_png_end[] asm("_binary_home_png_end");

extern const uint8_t tag_png_start[] asm("_binary_tag_png_start");
extern const uint8_t tag_png_end[] asm("_binary_tag_png_end");

extern const uint8_t apps_png_start[] asm("_binary_apps_png_start");
extern const uint8_t apps_png_end[] asm("_binary_apps_png_end");

extern const uint8_t hatchery_png_start[] asm("_binary_hatchery_png_start");
extern const uint8_t hatchery_png_end[] asm("_binary_hatchery_png_end");

extern const uint8_t dev_png_start[] asm("_binary_dev_png_start");
extern const uint8_t dev_png_end[] asm("_binary_dev_png_end");

extern const uint8_t settings_png_start[] asm("_binary_settings_png_start");
extern const uint8_t settings_png_end[] asm("_binary_settings_png_end");

extern const uint8_t update_png_start[] asm("_binary_update_png_start");
extern const uint8_t update_png_end[] asm("_binary_update_png_end");

typedef enum action {
    ACTION_NONE,
    ACTION_APPS,
    ACTION_LAUNCHER,
    ACTION_HATCHERY,
    ACTION_NAMETAG,
    ACTION_DEV,
    ACTION_SETTINGS,
    ACTION_UPDATE,
    ACTION_OTA
} menu_start_action_t;

void render_battery(pax_buf_t* pax_buffer, uint8_t percentage, bool charging) {
    float width     = 30;
    float height    = 34 - 15;
    float x         = pax_buffer->width - width - 10;
    float y         = (34 - height) / 2;
    float margin    = 3;
    float bar_width = width - (margin * 2);
    uint32_t color = (charging) ? 0xffffe700 : ((percentage > 10) ? 0xff40eb34 : 0xffeb4034);
    pax_simple_rect(pax_buffer, color, x, y, width, height);
    pax_simple_rect(pax_buffer, color, x + width, y + 5, 3, height - 10);
    pax_simple_rect(pax_buffer, 0xFF491d88, x + margin + ((percentage * bar_width) / 100), y + margin, bar_width - ((percentage * bar_width) / 100),
                    height - (margin * 2));
}

void render_start_help(pax_buf_t* pax_buffer, const char* text) {
    const pax_font_t* font = pax_font_saira_regular;
    pax_background(pax_buffer, 0xffffffff);
    pax_noclip(pax_buffer);
    pax_simple_rect(pax_buffer, 0xff491d88, 0, 220, 320, 20);
    pax_draw_text(pax_buffer, 0xffffffff, font, 18, 5, 240 - 18, "🅰 accept");
    pax_vec1_t version_size = pax_text_size(font, 18, text);
    pax_draw_text(pax_buffer, 0xffffffff, font, 18, 320 - 5 - version_size.x, 240 - 18, text);
}

void menu_start(xQueueHandle button_queue, const char* version) {
    pax_buf_t* pax_buffer = get_pax_buffer();
    menu_t*    menu       = menu_alloc("Main menu", 34, 18);

    menu->fgColor           = 0xFF000000;
    menu->bgColor           = 0xFFFFFFFF;
    menu->bgTextColor       = 0xFF000000;
    menu->selectedItemColor = 0xFFfec859;
    menu->borderColor       = 0xFF491d88;
    menu->titleColor        = 0xFFfec859;
    menu->titleBgColor      = 0xFF491d88;
    menu->scrollbarBgColor  = 0xFFCCCCCC;
    menu->scrollbarFgColor  = 0xFF555555;

    pax_buf_t icon_home;
    pax_decode_png_buf(&icon_home, (void*) home_png_start, home_png_end - home_png_start, PAX_BUF_32_8888ARGB, 0);
    pax_buf_t icon_tag;
    pax_decode_png_buf(&icon_tag, (void*) tag_png_start, tag_png_end - tag_png_start, PAX_BUF_32_8888ARGB, 0);
    pax_buf_t icon_apps;
    pax_decode_png_buf(&icon_apps, (void*) apps_png_start, apps_png_end - apps_png_start, PAX_BUF_32_8888ARGB, 0);
    pax_buf_t icon_hatchery;
    pax_decode_png_buf(&icon_hatchery, (void*) hatchery_png_start, hatchery_png_end - hatchery_png_start, PAX_BUF_32_8888ARGB, 0);
    pax_buf_t icon_dev;
    pax_decode_png_buf(&icon_dev, (void*) dev_png_start, dev_png_end - dev_png_start, PAX_BUF_32_8888ARGB, 0);
    pax_buf_t icon_settings;
    pax_decode_png_buf(&icon_settings, (void*) settings_png_start, settings_png_end - settings_png_start, PAX_BUF_32_8888ARGB, 0);
    pax_buf_t icon_update;
    pax_decode_png_buf(&icon_update, (void*) update_png_start, update_png_end - update_png_start, PAX_BUF_32_8888ARGB, 0);

    menu_set_icon(menu, &icon_home);
    menu_insert_item_icon(menu, "Name tag", NULL, (void*) ACTION_NAMETAG, -1, &icon_tag);
    menu_insert_item_icon(menu, "Apps", NULL, (void*) ACTION_LAUNCHER, -1, &icon_apps);
    menu_insert_item_icon(menu, "Hatchery", NULL, (void*) ACTION_HATCHERY, -1, &icon_hatchery);
    menu_insert_item_icon(menu, "Tools", NULL, (void*) ACTION_DEV, -1, &icon_dev);
    menu_insert_item_icon(menu, "Settings", NULL, (void*) ACTION_SETTINGS, -1, &icon_settings);
    menu_insert_item_icon(menu, "App update", NULL, (void*) ACTION_UPDATE, -1, &icon_update);
    menu_insert_item_icon(menu, "OS update", NULL, (void*) ACTION_OTA, -1, &icon_update);

    bool                render = true;
    menu_start_action_t action = ACTION_NONE;

    uint8_t analogReadTimer = 0;
    float   battery_voltage = 0;
    float   usb_voltage     = 0;

    uint8_t battery_percent  = 0;
    bool    battery_charging = false;

    RP2040* rp2040 = get_rp2040();

    while (1) {
        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(button_queue, &buttonMessage, 100 / portTICK_PERIOD_MS) == pdTRUE) {
            if (buttonMessage.state) {
                switch (buttonMessage.input) {
                    case RP2040_INPUT_JOYSTICK_DOWN:
                        menu_navigate_next_row(menu);
                        render = true;
                        break;
                    case RP2040_INPUT_JOYSTICK_UP:
                        menu_navigate_previous_row(menu);
                        render = true;
                        break;
                    case RP2040_INPUT_JOYSTICK_LEFT:
                        menu_navigate_previous(menu);
                        render = true;
                        break;
                    case RP2040_INPUT_JOYSTICK_RIGHT:
                        menu_navigate_next(menu);
                        render = true;
                        break;
                    case RP2040_INPUT_BUTTON_ACCEPT:
                    case RP2040_INPUT_JOYSTICK_PRESS:
                    case RP2040_INPUT_BUTTON_SELECT:
                    case RP2040_INPUT_BUTTON_START:
                        action = (menu_start_action_t) menu_get_callback_args(menu, menu_get_position(menu));
                        break;
                    default:
                        break;
                }
            }
        }

        if (analogReadTimer > 0) {
            analogReadTimer--;
        } else {
            analogReadTimer = 10;  // No need to update these values really quickly
            if (rp2040_read_vbat(rp2040, &battery_voltage) != ESP_OK) {
                battery_voltage = 0;
            }
            if (rp2040_read_vusb(rp2040, &usb_voltage) != ESP_OK) {
                usb_voltage = 0;
            }

            if (battery_voltage >= 3.6) {
                battery_percent = ((battery_voltage - 3.6) * 100) / (4.1 - 3.6);
                if (battery_percent > 100) battery_percent = 100;
            } else {
                battery_percent = 0;
            }

            battery_charging = (usb_voltage > 4.0) && (battery_percent < 100);

            render = true;
        }

        if (render) {
            char textBuffer[64];
            // snprintf(textBuffer, sizeof(textBuffer), "%u%%%c (%1.1fv) v%s", battery_percent, battery_charging ? '+' : ' ', battery_voltage, version);
            snprintf(textBuffer, sizeof(textBuffer), "v%s", version);
            render_start_help(pax_buffer, textBuffer);
            menu_render_grid(pax_buffer, menu, 0, 0, 320, 220);
            render_battery(pax_buffer, battery_percent, battery_charging);
            display_flush();
            render = false;
        }

        if (action != ACTION_NONE) {
            if (action == ACTION_HATCHERY) {
                menu_hatchery(button_queue);
            } else if (action == ACTION_NAMETAG) {
                show_nametag(button_queue);
            } else if (action == ACTION_SETTINGS) {
                menu_settings(button_queue);
            } else if (action == ACTION_DEV) {
                menu_dev(button_queue);
            } else if (action == ACTION_LAUNCHER) {
                menu_launcher(button_queue);
            } else if (action == ACTION_UPDATE) {
                update_apps(button_queue);
            } else if (action == ACTION_OTA) {
                ota_update(false);
            }
            action = ACTION_NONE;
            render = true;
        }
    }

    menu_free(menu);
    pax_buf_destroy(&icon_home);
    pax_buf_destroy(&icon_tag);
    pax_buf_destroy(&icon_apps);
    pax_buf_destroy(&icon_hatchery);
    pax_buf_destroy(&icon_dev);
    pax_buf_destroy(&icon_settings);
    pax_buf_destroy(&icon_update);
}
