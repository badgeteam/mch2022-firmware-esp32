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
#include "wifi_connection.h"
#include "wifi_ota.h"
#include "graphics_wrapper.h"

static const char *TAG = "wifi menu";

typedef enum action {
    /* ==== GENERIC ACTIONS ==== */
    // Nothing happens.
    ACTION_NONE,
    // Go back to the parent menu.
    ACTION_BACK,
    
    /* ==== MAIN MENU ACTIONS ==== */
    // Show the current WiFi settings.
    ACTION_SHOW,
    // Scan for networks and pick one to connect to.
    ACTION_SCAN,
    // Manually edit the current WiFi settings.
    ACTION_MANUAL,
    
    /* ==== AUTH MODES ==== */
    ACTION_AUTH_OPEN,
    ACTION_AUTH_WEP,
    ACTION_AUTH_WPA_PSK,
    ACTION_AUTH_WPA2_PSK,
    ACTION_AUTH_WPA_WPA2_PSK,
    ACTION_AUTH_WPA2_ENTERPRISE,
    ACTION_AUTH_WPA3_PSK,
    ACTION_AUTH_WPA2_WPA3_PSK,
    ACTION_AUTH_WAPI_PSK,
    
    /* ==== PHASE2 AUTH MODES ==== */
    ACTION_PHASE2_EAP,
    ACTION_PHASE2_MSCHAPV2,
    ACTION_PHASE2_MSCHAP,
    ACTION_PHASE2_PAP,
    ACTION_PHASE2_CHAP,
} menu_wifi_action_t;

void render_wifi_help(pax_buf_t* pax_buffer) {
    const pax_font_t *font = pax_get_font("saira regular");
    pax_background(pax_buffer, 0xFFFFFF);
    pax_noclip(pax_buffer);
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "[A] accept  [B] back");
}

void wifi_show(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341);
void wifi_setup(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, bool scan);
wifi_ap_record_t *wifi_scan_results(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, size_t num_aps, wifi_ap_record_t *aps);
int wifi_auth_menu(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, wifi_auth_mode_t default_mode);
int wifi_phase2_menu(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, esp_eap_ttls_phase2_types default_mode);

void menu_wifi(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    menu_t* menu = menu_alloc("WiFi configuration", 34, 18);
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
            menu_render(pax_buffer, menu, 0, 0, 320, 220, 0xFF491d88);
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
    snprintf(buffer, sizeof(buffer), "WiFi SSID:\n%s\nWiFi password:\n%s", ssid, password);
    pax_draw_text(pax_buffer, 0xFF000000, NULL, 18, 0, 0, buffer);
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

wifi_ap_record_t *wifi_scan_results(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, size_t num_aps, wifi_ap_record_t *aps) {
    menu_t *menu = menu_alloc("Select network", 20, 18);
    wifi_ap_record_t *picked = NULL;
    
    render_wifi_help(pax_buffer);
    
    for (size_t i = 0; i < num_aps; i++) {
        menu_insert_item(menu, (const char*) aps[i].ssid, NULL, (void *) (i + 1), -1);
    }
    
    bool render = true;
    size_t selection = 0;
    while (1) {
        rp2040_input_message_t buttonMessage = {0};
        selection = -1;
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
                        selection = 0;
                    }
                    break;
                case RP2040_INPUT_BUTTON_ACCEPT:
                case RP2040_INPUT_JOYSTICK_PRESS:
                case RP2040_INPUT_BUTTON_SELECT:
                case RP2040_INPUT_BUTTON_START:
                    if (value) {
                        selection = (size_t) menu_get_callback_args(menu, menu_get_position(menu));
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

        if (selection != (size_t) -1) {
            if (selection == 0) {
                break;
            } else {
                // You picked one, yay!
                picked = &aps[selection-1];
                break;
            }
            render = true;
            selection = -1;
            render_wifi_help(pax_buffer);
        }
    }
    
    menu_free(menu);
    return picked;
}

int wifi_auth_menu(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, wifi_auth_mode_t default_mode) {
    menu_t* menu = menu_alloc("Authentication mode", 20, 18);
    menu_insert_item(menu, "Insecure", NULL, (void*) ACTION_AUTH_OPEN, -1);
    menu_insert_item(menu, "WEP", NULL, (void*) ACTION_AUTH_WEP, -1);
    menu_insert_item(menu, "WPA PSK", NULL, (void*) ACTION_AUTH_WPA_PSK, -1);
    menu_insert_item(menu, "WPA2 PSK", NULL, (void*) ACTION_AUTH_WPA2_PSK, -1);
    // menu_insert_item(menu, "QQQQQQQQQQQQ", NULL, (void*) ACTION_AUTH_WPA_WPA2_PSK, -1);
    menu_insert_item(menu, "WPA2 Enterprise", NULL, (void*) ACTION_AUTH_WPA2_ENTERPRISE, -1);
    menu_insert_item(menu, "WPA3 PSK", NULL, (void*) ACTION_AUTH_WPA3_PSK, -1);
    // menu_insert_item(menu, "QQQQQQQQQQQQ", NULL, (void*) ACTION_AUTH_WPA2_WPA3_PSK, -1);
    // menu_insert_item(menu, "WAPI PSK", NULL, (void*) ACTION_AUTH_WAPI_PSK, -1);
    
    // Pre-select default authmode.
    for (int i = 0; i < menu_get_length(menu); i++) {
        if ((int) menu_get_callback_args(menu, i) - (int) ACTION_AUTH_OPEN == (int) default_mode) {
            menu_navigate_to(menu, i);
        }
    }
    
    bool render = true;
    menu_wifi_action_t action = ACTION_NONE;
    wifi_auth_mode_t pick = default_mode;

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
            if (action == ACTION_BACK) {
                pick = -1;
                break;
            } else {
                pick = (wifi_auth_mode_t) (action - ACTION_AUTH_OPEN);
                break;
            }
            render = true;
            action = ACTION_NONE;
            render_wifi_help(pax_buffer);
        }
    }

    menu_free(menu);
    return pick;
}

int wifi_phase2_menu(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, esp_eap_ttls_phase2_types default_mode) {
    menu_t* menu = menu_alloc("Phase 2 authentication mode", 20, 18);
    menu_insert_item(menu, "ESP", NULL, (void*) ACTION_PHASE2_EAP, -1);
    menu_insert_item(menu, "MSCHAPv2", NULL, (void*) ACTION_PHASE2_MSCHAPV2, -1);
    menu_insert_item(menu, "MSCHAP", NULL, (void*) ACTION_PHASE2_MSCHAP, -1);
    menu_insert_item(menu, "PAP", NULL, (void*) ACTION_PHASE2_PAP, -1);
    menu_insert_item(menu, "CHAP", NULL, (void*) ACTION_PHASE2_CHAP, -1);
    
    // Pre-select default authmode.
    for (int i = 0; i < menu_get_length(menu); i++) {
        if ((int) menu_get_callback_args(menu, i) - (int) ACTION_PHASE2_EAP == (int) default_mode) {
            menu_navigate_to(menu, i);
        }
    }
    
    bool render = true;
    menu_wifi_action_t action = ACTION_NONE;
    esp_eap_ttls_phase2_types pick = default_mode;

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
            if (action == ACTION_BACK) {
                break;
            } else {
                pick = (wifi_auth_mode_t) (action - ACTION_PHASE2_EAP);
                break;
            }
            render = true;
            action = ACTION_NONE;
            render_wifi_help(pax_buffer);
        }
    }

    menu_free(menu);
    return pick;
}

// Sorts WiFi APs by RSSI (best RSSI first in the list).
static int wifi_ap_sorter(const void *a0, const void *b0) {
    const wifi_ap_record_t *a = a0;
    const wifi_ap_record_t *b = b0;
    return b->rssi - a->rssi;
}

void wifi_setup(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, bool scan) {
    
    char ssid[33] = {0};
    char username[33] = {0};
    char password[65] = {0};
    nvs_handle_t handle;
    nvs_open("system", NVS_READWRITE, &handle);
    bool accepted = true;
    wifi_auth_mode_t authmode = WIFI_AUTH_WPA2_PSK;
    esp_eap_ttls_phase2_types phase2 = ESP_EAP_TTLS_PHASE2_EAP;
    
    /* ==== scanning phase ==== */
    if (scan) {
        // Show a little bit of text.
        display_boot_screen(pax_buffer, ili9341, "Scanning WiFi networks...");
        
        // Scan for networks.
        wifi_ap_record_t *aps;
        size_t n_aps = wifi_scan(&aps);
        
        // Sort them by RSSI.
        qsort(aps, n_aps, sizeof(wifi_ap_record_t), wifi_ap_sorter);
        
        // Make a de-duplicated list.
        wifi_ap_record_t *dedup = malloc(sizeof(wifi_ap_record_t) * n_aps);
        size_t n_dedup = 0;
        for (size_t i = 0; i < n_aps; ) {
            for (size_t x = 0; x < n_dedup; x++) {
                if (!strcmp((const char *) aps[i].ssid, (const char *) dedup[x].ssid)) goto cont;
            }
            dedup[n_dedup] = aps[i];
            n_dedup ++;
            cont:
            i++;
        }
        
        // Open a little menu for picking a network.
        wifi_ap_record_t *pick = wifi_scan_results(buttonQueue, pax_buffer, ili9341, n_dedup, dedup);
        if (!pick) {
            nvs_close(handle);
            return;
        }
        // Copy the SSID in.
        memcpy(ssid, pick->ssid, sizeof(ssid));
        authmode = pick->authmode;
        
        // Free memories.
        free(aps);
        free(dedup);
    } else {
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
            
            res = nvs_get_str(handle, "wifi.username", NULL, &requiredSize);
            if (res != ESP_OK) {
                strcpy(username, "");
            } else if (requiredSize < sizeof(username)) {
                res = nvs_get_str(handle, "wifi.username", username, &requiredSize);
                if (res != ESP_OK) strcpy(username, "");
            }
        }
        
        // Select SSID.
        accepted = keyboard(buttonQueue, pax_buffer, ili9341, 30, 30, pax_buffer->width - 60, pax_buffer->height - 60, "WiFi SSID", "Press HOME to cancel", ssid, sizeof(ssid));
        
        // Select auth mode.
        if (accepted) {
            uint8_t default_auth = authmode;
            nvs_get_u8(handle, "wifi.authmode", &default_auth);
            authmode = wifi_auth_menu(buttonQueue, pax_buffer, ili9341, default_auth);
            accepted = authmode != -1;
        }
    }
    
    /* ==== manual entering phase ==== */
    if (authmode == WIFI_AUTH_WPA2_ENTERPRISE) {
        if (accepted) {
            // Phase2 method.
            uint8_t default_auth = authmode;
            nvs_get_u8(handle, "wifi.phase2", &default_auth);
            phase2 = wifi_phase2_menu(buttonQueue, pax_buffer, ili9341, default_auth);
            accepted = phase2 != -1;
        }
        if (accepted) {
            // Username.
            accepted = keyboard(buttonQueue, pax_buffer, ili9341, 30, 30, pax_buffer->width - 60, pax_buffer->height - 60, "WiFi username", "Press HOME to cancel", username, sizeof(username));
        }
    }
    if (accepted) {
        // Password.
        accepted = keyboard(buttonQueue, pax_buffer, ili9341, 30, 30, pax_buffer->width - 60, pax_buffer->height - 60, "WiFi password", "Press HOME to cancel", password, sizeof(password));
    }
    if (accepted) {
        nvs_set_str(handle, "wifi.ssid", ssid);
        nvs_set_str(handle, "wifi.password", password);
        nvs_set_u8 (handle, "wifi.authmode", authmode);
        if (authmode == WIFI_AUTH_WPA2_ENTERPRISE) {
            nvs_set_str(handle, "wifi.username", username);
            nvs_set_u8 (handle, "wifi.phase2", phase2);
        }
        display_boot_screen(pax_buffer, ili9341, "WiFi settings stored");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    nvs_close(handle);
}

