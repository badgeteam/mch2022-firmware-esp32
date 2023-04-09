#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <nvs.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "appfs.h"
#include "appfs_wrapper.h"
#include "bootscreen.h"
#include "filesystems.h"
#include "graphics_wrapper.h"
#include "hardware.h"
#include "menu.h"
#include "nametag.h"
#include "pax_codecs.h"
#include "pax_gfx.h"
#include "rp2040.h"
#include "rp2040_updater.h"
#include "system_wrapper.h"
#include "wifi.h"
#include "wifi_connect.h"
#include "wifi_ota.h"

extern const uint8_t settings_png_start[] asm("_binary_settings_png_start");
extern const uint8_t settings_png_end[] asm("_binary_settings_png_end");

typedef enum action {
    ACTION_NONE,
    ACTION_BACK,
    ACTION_WIFI,
    ACTION_OTA,
    ACTION_OTA_NIGHTLY,
    ACTION_RP2040_BL,
    ACTION_NICKNAME,
    ACTION_BRIGHTNESS,
    ACTION_LOCK,
    ACTION_FORMAT_FAT,
    ACTION_FORMAT_APPFS
} menu_settings_action_t;

void edit_lock(xQueueHandle button_queue) {
    pax_buf_t*   pax_buffer = get_pax_buffer();
    nvs_handle_t handle;
    esp_err_t    res = nvs_open("system", NVS_READWRITE, &handle);
    if (res != ESP_OK) {
        ESP_LOGE("settings", "NVS open failed: %d", res);
        return;
    }
    uint8_t state;
    res = nvs_get_u8(handle, "flash_lock", &state);
    if (res != ESP_OK) {
        state = 0x01;
    }
    bool quit = false;
    while (!quit) {
        const pax_font_t* font = pax_font_saira_regular;
        pax_noclip(pax_buffer);
        pax_background(pax_buffer, 0xFFFFFF);
        pax_draw_text(pax_buffer, 0xFF000000, font, 23, 0, 20 * 0, "Flashing lock");
        char state_str[64];
        snprintf(state_str, sizeof(state_str), "State: %s\n", state ? "active" : "disabled");
        pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 1, state_str);
        pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "🅰 toggle state  🅱 back");
        display_flush();
        if (wait_for_button()) {
            state = (~state) & 0x01;
            nvs_set_u8(handle, "flash_lock", state);
            nvs_commit(handle);
        } else {
            quit = true;
        }
    }
    nvs_close(handle);
}

uint8_t wait_for_button_adv() {
    RP2040* rp2040 = get_rp2040();
    if (rp2040 == NULL) return false;
    while (1) {
        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(rp2040->queue, &buttonMessage, portMAX_DELAY) == pdTRUE) {
            if (buttonMessage.state) {
                return buttonMessage.input;
            }
        }
    }
}

void edit_brightness(xQueueHandle button_queue) {
    pax_buf_t*   pax_buffer = get_pax_buffer();
    nvs_handle_t handle;
    esp_err_t    res = nvs_open("system", NVS_READWRITE, &handle);
    if (res != ESP_OK) {
        ESP_LOGE("settings", "NVS open failed: %d", res);
        return;
    }
    uint8_t state;
    res = nvs_get_u8(handle, "brightness", &state);
    if (res != ESP_OK) {
        state = 0xFF;
    }
    bool quit = false;
    while (!quit) {
        const pax_font_t* font = pax_font_saira_regular;
        pax_noclip(pax_buffer);
        pax_background(pax_buffer, 0xFFFFFF);
        pax_draw_text(pax_buffer, 0xFF000000, font, 23, 0, 20 * 0, "Screen brightness");
        char state_str[64];
        snprintf(state_str, sizeof(state_str), "Brightness: %u%%\n", (state * 100) / 255);
        pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 1, state_str);
        pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "Increase / decrease  🅱 back");
        display_flush();
        uint8_t button = wait_for_button_adv(button_queue);
        switch (button) {
            case RP2040_INPUT_BUTTON_BACK:
            case RP2040_INPUT_BUTTON_HOME:
                quit = true;
                break;
            case RP2040_INPUT_JOYSTICK_UP:
            case RP2040_INPUT_JOYSTICK_RIGHT:
                if (state > 245) {
                    state = 255;
                } else {
                    state += 10;
                }
                nvs_set_u8(handle, "brightness", state);
                nvs_commit(handle);
                rp2040_set_lcd_backlight(get_rp2040(), state);
                break;
            case RP2040_INPUT_JOYSTICK_DOWN:
            case RP2040_INPUT_JOYSTICK_LEFT:
                if (state < 16) {
                    state = 16;
                } else {
                    state -= 10;
                }
                nvs_set_u8(handle, "brightness", state);
                nvs_commit(handle);
                rp2040_set_lcd_backlight(get_rp2040(), state);
                break;
            default:
                break;
        }
    }
    nvs_close(handle);
}

void render_settings_help(pax_buf_t* pax_buffer) {
    const pax_font_t* font = pax_font_saira_regular;
    pax_background(pax_buffer, 0xFFFFFF);
    pax_noclip(pax_buffer);
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "🅰 accept  🅱 back");
}

void menu_settings(xQueueHandle button_queue) {
    pax_buf_t* pax_buffer = get_pax_buffer();
    menu_t*    menu       = menu_alloc("Settings", 34, 18);

    menu->fgColor           = 0xFF000000;
    menu->bgColor           = 0xFFFFFFFF;
    menu->bgTextColor       = 0xFFFFFFFF;
    menu->selectedItemColor = 0xFF491d88;
    menu->borderColor       = 0xFF43b5a0;
    menu->titleColor        = 0xFF491d88;
    menu->titleBgColor      = 0xFF43b5a0;
    menu->scrollbarBgColor  = 0xFFCCCCCC;
    menu->scrollbarFgColor  = 0xFF555555;

    pax_buf_t icon_settings;
    pax_decode_png_buf(&icon_settings, (void*) settings_png_start, settings_png_end - settings_png_start, PAX_BUF_32_8888ARGB, 0);

    menu_set_icon(menu, &icon_settings);

    menu_insert_item(menu, "Edit nickname", NULL, (void*) ACTION_NICKNAME, -1);
    menu_insert_item(menu, "WiFi settings", NULL, (void*) ACTION_WIFI, -1);
    menu_insert_item(menu, "Screen brightness", NULL, (void*) ACTION_BRIGHTNESS, -1);
    menu_insert_item(menu, "Firmware update", NULL, (void*) ACTION_OTA, -1);
    menu_insert_item(menu, "Firmware flashing lock", NULL, (void*) ACTION_LOCK, -1);
    menu_insert_item(menu, "Flash RP2040 firmware", NULL, (void*) ACTION_RP2040_BL, -1);
    menu_insert_item(menu, "Install experimental firmware", NULL, (void*) ACTION_OTA_NIGHTLY, -1);
    menu_insert_item(menu, "Format internal FAT filesystem", NULL, (void*) ACTION_FORMAT_FAT, -1);
    menu_insert_item(menu, "Format internal AppFS filesystem", NULL, (void*) ACTION_FORMAT_APPFS, -1);

    bool                   render = true;
    menu_settings_action_t action = ACTION_NONE;

    render_settings_help(pax_buffer);

    while (1) {
        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(button_queue, &buttonMessage, 16 / portTICK_PERIOD_MS) == pdTRUE) {
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
            menu_render(pax_buffer, menu, 0, 0, 320, 220);
            display_flush();
            render = false;
        }

        if (action != ACTION_NONE) {
            if (action == ACTION_RP2040_BL) {
                display_boot_screen("Please wait...");
                rp2040_update_start(get_rp2040());
            } else if (action == ACTION_OTA) {
                ota_update(false);
            } else if (action == ACTION_OTA_NIGHTLY) {
                ota_update(true);
            } else if (action == ACTION_WIFI) {
                menu_wifi(button_queue);
            } else if (action == ACTION_BACK) {
                break;
            } else if (action == ACTION_NICKNAME) {
                edit_nickname(button_queue);
            } else if (action == ACTION_LOCK) {
                edit_lock(button_queue);
            } else if (action == ACTION_FORMAT_FAT) {
                display_boot_screen("Formatting FAT...");
                format_internal_filesystem();
            } else if (action == ACTION_FORMAT_APPFS) {
                display_boot_screen("Formatting AppFS...");
                appfsFormat();
            } else if (action == ACTION_BRIGHTNESS) {
                edit_brightness(button_queue);
            }

            render = true;
            action = ACTION_NONE;
            render_settings_help(pax_buffer);
        }
    }

    menu_free(menu);
    pax_buf_destroy(&icon_settings);
}
