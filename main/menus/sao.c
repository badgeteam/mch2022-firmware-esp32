#include "sao.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "app_management.h"
#include "appfs.h"
#include "appfs_wrapper.h"
#include "cJSON.h"
#include "graphics_wrapper.h"
#include "gui_element_header.h"
#include "hardware.h"
#include "http_download.h"
#include "menu.h"
#include "pax_codecs.h"
#include "pax_gfx.h"
#include "rp2040.h"
#include "sao_eeprom.h"
#include "settings.h"
#include "system_wrapper.h"
#include "wifi_connect.h"

void program_googly() {
    sao_driver_storage_data_t data = {.flags         = 0,  // No LEDs
                                      .address       = 0x50,
                                      .size_exp      = 15,  // 32 kbit (2^15)
                                      .page_size_exp = 6,   // 64 bytes (2^6)
                                      .data_offset   = 1,   // 1 page (64 bytes)
                                      .reserved      = 0};
    sao_format("Googly eye", SAO_DRIVER_STORAGE_NAME, (uint8_t*) &data, sizeof(data), NULL, NULL, 0);
}

void program_cloud() {
    sao_driver_storage_data_t data = {.flags         = SAO_DRIVER_STORAGE_FLAG_IO1_LED | SAO_DRIVER_STORAGE_FLAG_IO2_LED,  // Both IO pins have a LED attached
                                      .address       = 0x50,
                                      .size_exp      = 15,  // 32 kbit (2^15)
                                      .page_size_exp = 6,   // 64 bytes (2^6)
                                      .data_offset   = 1,   // 1 page (64 bytes)
                                      .reserved      = 0};
    sao_format("Cloud", SAO_DRIVER_STORAGE_NAME, (uint8_t*) &data, sizeof(data), NULL, NULL, 0);
}

void program_cloud_tilde() {
    sao_driver_neopixel_data_t data_neopixel = {.length      = 2,
                                                .color_order = SAO_DRIVER_NEOPIXEL_COLOR_ORDER_GRB,  // WS2812
                                                .reserved    = 0};

    sao_driver_storage_data_t data_storage = {.flags         = 0,
                                              .address       = 0x50,
                                              .size_exp      = 15,  // 32 kbit (2^15)
                                              .page_size_exp = 6,   // 64 bytes (2^6)
                                              .data_offset   = 1,   // 1 page (64 bytes)
                                              .reserved      = 0};

    sao_format("Cloud", SAO_DRIVER_NEOPIXEL_NAME, (uint8_t*) &data_neopixel, sizeof(data_neopixel), SAO_DRIVER_STORAGE_NAME, (uint8_t*) &data_storage,
               sizeof(data_storage));
}

void program_cassette() {
    sao_driver_storage_data_t data = {.flags         = SAO_DRIVER_STORAGE_FLAG_IO1_LED | SAO_DRIVER_STORAGE_FLAG_IO2_LED,  // Both IO pins have a LED attached
                                      .address       = 0x50,
                                      .size_exp      = 15,  // 32 kbit (2^15)
                                      .page_size_exp = 6,   // 64 bytes (2^6)
                                      .data_offset   = 1,   // 1 page (64 bytes)
                                      .reserved      = 0};
    sao_format("Cassette", SAO_DRIVER_STORAGE_NAME, (uint8_t*) &data, sizeof(data), NULL, NULL, 0);
}

void program_diskette() {
    sao_driver_storage_data_t data = {.flags         = SAO_DRIVER_STORAGE_FLAG_IO1_LED | SAO_DRIVER_STORAGE_FLAG_IO2_LED,  // Both IO pins have a LED attached
                                      .address       = 0x50,
                                      .size_exp      = 15,  // 32 kbit (2^15)
                                      .page_size_exp = 6,   // 64 bytes (2^6)
                                      .data_offset   = 1,   // 1 page (64 bytes)
                                      .reserved      = 0};
    sao_format("Diskette", SAO_DRIVER_STORAGE_NAME, (uint8_t*) &data, sizeof(data), NULL, NULL, 0);
}

void program_ssd1306() {
    sao_driver_ssd1306_data_t data_ssd1306 = {.address = 0x3C, .height = 64, .reserved = 0};

    sao_driver_storage_data_t data_storage = {.flags   = SAO_DRIVER_STORAGE_FLAG_IO1_LED | SAO_DRIVER_STORAGE_FLAG_IO2_LED,  // Both IO pins have a LED attached
                                              .address = 0x50,
                                              .size_exp      = 15,  // 32 kbit (2^15)
                                              .page_size_exp = 6,   // 64 bytes (2^6)
                                              .data_offset   = 1,   // 1 page (64 bytes)
                                              .reserved      = 0};

    sao_format("OLED display", SAO_DRIVER_SSD1306_NAME, (uint8_t*) &data_ssd1306, sizeof(data_ssd1306), SAO_DRIVER_STORAGE_NAME, (uint8_t*) &data_storage,
               sizeof(data_storage));
}

void program_ntag() {
    sao_driver_ntag_data_t data_ntag = {.address  = 0x55,
                                        .size_exp = 10,  // 1k (NT3H2111)
                                        .reserved = 0};

    sao_driver_storage_data_t data_storage = {
        .flags   = SAO_DRIVER_STORAGE_FLAG_IO1_LED | SAO_DRIVER_STORAGE_FLAG_IO2_INTERRUPT,  // IO1 has a LED attached, IO2 is the interrupt line of the NTAG IC
        .address = 0x50,
        .size_exp      = 15,  // 32 kbit (2^15)
        .page_size_exp = 6,   // 64 bytes (2^6)
        .data_offset   = 1,   // 1 page (64 bytes)
        .reserved      = 0};

    sao_format("NFC tag", SAO_DRIVER_NTAG_NAME, (uint8_t*) &data_ntag, sizeof(data_ntag), SAO_DRIVER_STORAGE_NAME, (uint8_t*) &data_storage,
               sizeof(data_storage));
}

typedef enum action {
    ACTION_NONE = 0,
    ACTION_BACK,
    ACTION_GOOGLY,
    ACTION_CLOUD,
    ACTION_CLOUD_TILDE,
    ACTION_CASSETTE,
    ACTION_DISKETTE,
    ACTION_SSD1306,
    ACTION_NTAG
} menu_dev_action_t;

static void menu_sao_format(xQueueHandle button_queue) {
    pax_buf_t* pax_buffer = get_pax_buffer();
    menu_t*    menu       = menu_alloc("Program EEPROM", 20, 18);

    menu->fgColor           = 0xFF000000;
    menu->bgColor           = 0xFFFFFFFF;
    menu->bgTextColor       = 0xFF000000;
    menu->selectedItemColor = 0xFFfec859;
    menu->borderColor       = 0xFFfa448c;
    menu->titleColor        = 0xFFfec859;
    menu->titleBgColor      = 0xFFfa448c;
    menu->scrollbarBgColor  = 0xFFCCCCCC;
    menu->scrollbarFgColor  = 0xFF555555;

    menu_insert_item(menu, "Googly eye", NULL, (void*) ACTION_GOOGLY, -1);
    menu_insert_item(menu, "Cloud (with normal LEDs)", NULL, (void*) ACTION_CLOUD, -1);
    menu_insert_item(menu, "Cloud (with neopixels)", NULL, (void*) ACTION_CLOUD_TILDE, -1);
    menu_insert_item(menu, "Cassette", NULL, (void*) ACTION_CASSETTE, -1);
    menu_insert_item(menu, "Diskette", NULL, (void*) ACTION_DISKETTE, -1);
    menu_insert_item(menu, "SSD1306", NULL, (void*) ACTION_SSD1306, -1);
    menu_insert_item(menu, "NTAG", NULL, (void*) ACTION_NTAG, -1);

    bool              render = true;
    menu_dev_action_t action = ACTION_NONE;

    while (1) {
        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(button_queue, &buttonMessage, 16 / portTICK_PERIOD_MS) == pdTRUE) {
            uint8_t pin   = buttonMessage.input;
            bool    value = buttonMessage.state;
            if (value) {
                switch (pin) {
                    case RP2040_INPUT_JOYSTICK_DOWN:
                        menu_navigate_next(menu);
                        render = true;
                        break;
                    case RP2040_INPUT_JOYSTICK_UP:
                        menu_navigate_previous(menu);
                        render = true;
                        break;
                    case RP2040_INPUT_BUTTON_HOME:
                    case RP2040_INPUT_BUTTON_BACK:
                        action = ACTION_BACK;
                        break;
                    case RP2040_INPUT_BUTTON_ACCEPT:
                    case RP2040_INPUT_JOYSTICK_PRESS:
                    case RP2040_INPUT_BUTTON_SELECT:
                    case RP2040_INPUT_BUTTON_START:
                        action = (menu_dev_action_t) menu_get_callback_args(menu, menu_get_position(menu));
                        break;
                    default:
                        break;
                }
            }
        }

        if (render) {
            menu_render(pax_buffer, menu, 40, 40, 320 - 90, 220 - 90);
            display_flush();
            render = false;
        }

        if (action != ACTION_NONE) {
            if (action == ACTION_GOOGLY) {
                program_googly();
                break;
            } else if (action == ACTION_CLOUD) {
                program_cloud();
                break;
            } else if (action == ACTION_CLOUD_TILDE) {
                program_cloud_tilde();
                break;
            } else if (action == ACTION_CASSETTE) {
                program_cassette();
                break;
            } else if (action == ACTION_DISKETTE) {
                program_diskette();
                break;
            } else if (action == ACTION_SSD1306) {
                program_ssd1306();
                break;
            } else if (action == ACTION_NTAG) {
                program_ntag();
                break;
            } else if (action == ACTION_BACK) {
                break;
            }
            action = ACTION_NONE;
            render = true;
        }
    }

    menu_free(menu);
}

bool sao_is_app_installed(char* name) {
    appfs_handle_t appfs_fd = appfsNextEntry(APPFS_INVALID_FD);
    while (appfs_fd != APPFS_INVALID_FD) {
        const char* slug    = NULL;
        const char* title   = NULL;
        uint16_t    version = 0xFFFF;
        appfsEntryInfoExt(appfs_fd, &slug, &title, &version, NULL);
        if ((slug != NULL) && (strlen(slug) == strlen(name)) && (strcmp(slug, name) == 0)) {
            return true;
        }
        appfs_fd = appfsNextEntry(appfs_fd);
    }
    return false;
}

void sao_start_app(char* name) {
    appfs_handle_t appfs_fd = appfsNextEntry(APPFS_INVALID_FD);
    while (appfs_fd != APPFS_INVALID_FD) {
        const char* slug    = NULL;
        const char* title   = NULL;
        uint16_t    version = 0xFFFF;
        appfsEntryInfoExt(appfs_fd, &slug, &title, &version, NULL);
        if ((slug != NULL) && (strlen(slug) == strlen(name)) && (strcmp(slug, name) == 0)) {
            appfs_boot_app(appfs_fd);
        }
        appfs_fd = appfsNextEntry(appfs_fd);
    }
}

bool sao_install_app(xQueueHandle button_queue, char* name) {
    char*  data_app_info = NULL;
    size_t size_app_info = 0;
    cJSON* json_app_info = NULL;
    char   url[128];
    snprintf(url, sizeof(url) - 1, "https://mch2022.badge.team/v2/mch2022/%s/%s/%s", "esp32", "sao", name);
    bool success = download_ram(url, (uint8_t**) &data_app_info, &size_app_info);
    if (!success) return false;
    if (data_app_info == NULL) return false;
    json_app_info = cJSON_ParseWithLength(data_app_info, size_app_info);
    if (json_app_info == NULL) return false;
    bool res = install_app(button_queue, "esp32", false, data_app_info, size_app_info, json_app_info);
    cJSON_Delete(json_app_info);
    free(data_app_info);
    return res;
}

static void render_sao_status(pax_buf_t* pax_buffer, SAO* sao) {
    const pax_font_t* font = pax_font_saira_regular;
    pax_background(pax_buffer, 0xFFFFFF);
    pax_noclip(pax_buffer);

    if (sao->type == SAO_BINARY) {
        render_header(pax_buffer, 0, 0, pax_buffer->width, 34, 18, 0xFF491d88, 0xFF43b5a0, NULL, sao->name);
        if (strncmp(sao->driver, SAO_DRIVER_APP_NAME, strlen(SAO_DRIVER_APP_NAME)) == 0) {
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 40, "This SAO wants to start an app:");
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 60, (char*) sao->driver_data);
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "🅰️ Start app 🅱 back 🆂 Program");
        } else if (strncmp(sao->driver, SAO_DRIVER_STORAGE_NAME, strlen(SAO_DRIVER_STORAGE_NAME)) == 0) {
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 40, "EEPROM data storage");
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "🅱 back 🆂 Program");
        } else if (strncmp(sao->driver, SAO_DRIVER_NEOPIXEL_NAME, strlen(SAO_DRIVER_NEOPIXEL_NAME)) == 0) {
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 40, "Neopixel LEDs");
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "🅱 back 🆂 Program");
        } else if (strncmp(sao->driver, SAO_DRIVER_SSD1306_NAME, strlen(SAO_DRIVER_SSD1306_NAME)) == 0) {
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 40, "SSD1306 OLED display");
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "🅱 back 🆂 Program");
        } else if (strncmp(sao->driver, SAO_DRIVER_NTAG_NAME, strlen(SAO_DRIVER_NTAG_NAME)) == 0) {
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 40, "NFC tag");
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "🅱 back 🆂 Program");
        } else {
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 40, "SAO with unknown driver type");
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 55, sao->driver);
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "🅱 back 🆂 Program");
        }
    } else if (sao->type == SAO_JSON) {
        render_header(pax_buffer, 0, 0, pax_buffer->width, 34, 18, 0xFF491d88, 0xFF43b5a0, NULL, "JSON SAO");
        pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 40, "SAO with JSON data detected\nThis type of metadata is not supported");
        pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "🅱 back 🆂 Program");
    } else {
        render_header(pax_buffer, 0, 0, pax_buffer->width, 34, 18, 0xFF491d88, 0xFF43b5a0, NULL, "Unformatted SAO");
        pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "🅱 back 🆂 Program");
    }
}

static void render_sao_not_detected(pax_buf_t* pax_buffer) {
    const pax_font_t* font = pax_font_saira_regular;
    pax_background(pax_buffer, 0xFFAAAA);
    pax_noclip(pax_buffer);
    render_header(pax_buffer, 0, 0, pax_buffer->width, 34, 18, 0xFF491d88, 0xFF43b5a0, NULL, "SAO");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 40, "No SAO detected");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "🅱 back");
}

static bool connect_to_wifi() {
    render_message("Connecting to WiFi...");
    display_flush();
    if (!wifi_connect_to_stored()) {
        wifi_disconnect_and_disable();
        render_message("Unable to connect to\nthe WiFi network");
        display_flush();
        wait_for_button();
        return false;
    }
    return true;
}

void menu_sao(xQueueHandle button_queue) {
    rp2040_input_message_t buttonMessage = {0};
    while (xQueueReceive(button_queue, &buttonMessage, 0) == pdTRUE) {
        // Flush!
    }

    pax_buf_t* pax_buffer = get_pax_buffer();
    bool       exit       = false;
    while (!exit) {
        SAO sao = {0};
        sao_identify(&sao);

        // Update the EEPROM contents of the SAOs Renze handed out at MCH2022
        if (sao.type == SAO_BINARY) {
            // Update cloud SAOs with old data
            if (memcmp(sao.driver, "hatchery", strlen("hatchery")) == 0) {
                if (memcmp(sao.name, "cloud", strlen("cloud")) == 0) {
                    program_cloud();
                    continue;
                }
            }
            // Update cassette eye SAOs with old data
            if (memcmp(sao.driver, "hatchery", strlen("hatchery")) == 0) {
                if (memcmp(sao.name, "casette", strlen("casette")) == 0) {
                    program_cassette();
                    continue;
                } else if (memcmp(sao.name, "cassette", strlen("cassette")) == 0) {
                    program_cassette();
                    continue;
                }
            }
            // Update googly eye SAOs with old data
            if (memcmp(sao.driver, "hatchery", strlen("hatchery")) == 0) {
                if (memcmp(sao.name, "diskette", strlen("diskette")) == 0) {
                    program_diskette();
                    continue;
                }
            }
            // Update diskette SAOs with old data
            if (memcmp(sao.driver, SAO_DRIVER_STORAGE_NAME, strlen(SAO_DRIVER_STORAGE_NAME)) == 0) {
                if (memcmp(sao.name, "googly", strlen("googly")) == 0) {
                    program_googly();
                    continue;
                }
            }
        }

        if (sao.type != SAO_NONE) {
            render_sao_status(pax_buffer, &sao);
        } else {
            render_sao_not_detected(pax_buffer);
        }

        display_flush();

        if (xQueueReceive(button_queue, &buttonMessage, 200 / portTICK_PERIOD_MS) == pdTRUE) {
            uint8_t pin   = buttonMessage.input;
            bool    value = buttonMessage.state;
            if (value) {
                switch (pin) {
                    case RP2040_INPUT_BUTTON_START:
                        menu_sao_format(button_queue);
                        break;
                    case RP2040_INPUT_BUTTON_HOME:
                    case RP2040_INPUT_BUTTON_BACK:
                        exit = true;
                        break;
                    case RP2040_INPUT_BUTTON_ACCEPT:
                        if ((sao.type == SAO_BINARY) && (strncmp(sao.driver, "badgeteam_app_link", strlen("badgeteam_app_link")) == 0)) {
                            if (!sao_is_app_installed((char*) sao.driver_data)) {
                                if (connect_to_wifi()) {
                                    render_message("Installing app...");
                                    display_flush();
                                    if (sao_install_app(button_queue, (char*) sao.driver_data)) {
                                        sao_start_app((char*) sao.driver_data);
                                    } else {
                                        render_message("Failed to install app");
                                        display_flush();
                                        wait_for_button();
                                    }
                                }
                            } else {
                                render_message("Starting app...");
                                display_flush();
                                sao_start_app((char*) sao.driver_data);
                            }
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }
}
