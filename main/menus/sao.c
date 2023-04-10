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
    char magic[]       = "LIFE";
    char name[]        = "Googly eye";
    char driver[]      = "badgeteam_app_link";
    char driver_data[] = "nicolai_sao";

    size_t   length = 4 + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint8_t) + strlen(name) + strlen(driver) + strlen(driver_data);
    uint8_t* data   = malloc(length);
    memcpy(data, magic, 4);
    data[4] = strlen(name);
    data[5] = strlen(driver);
    data[6] = strlen(driver_data);
    data[7] = 0;
    memcpy(&data[8], name, strlen(name));
    memcpy(&data[8] + strlen(name), driver, strlen(driver));
    memcpy(&data[8] + strlen(name) + strlen(driver), driver_data, strlen(driver_data));
    sao_write_raw(0, data, length);
    free(data);
}

void program_cloud() {
    char magic[]       = "LIFE";
    char name[]        = "Cloud";
    char driver[]      = "badgeteam_app_link";
    char driver_data[] = "nicolai_sao";

    size_t   length = 4 + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint8_t) + strlen(name) + strlen(driver) + strlen(driver_data);
    uint8_t* data   = malloc(length);
    memcpy(data, magic, 4);
    data[4] = strlen(name);
    data[5] = strlen(driver);
    data[6] = strlen(driver_data);
    data[7] = 0;
    memcpy(&data[8], name, strlen(name));
    memcpy(&data[8] + strlen(name), driver, strlen(driver));
    memcpy(&data[8] + strlen(name) + strlen(driver), driver_data, strlen(driver_data));
    sao_write_raw(0, data, length);
    free(data);
}

void program_casette() {
    char magic[]       = "LIFE";
    char name[]        = "Casette";
    char driver[]      = "badgeteam_app_link";
    char driver_data[] = "nicolai_sao";

    size_t   length = 4 + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint8_t) + strlen(name) + strlen(driver) + strlen(driver_data);
    uint8_t* data   = malloc(length);
    memcpy(data, magic, 4);
    data[4] = strlen(name);
    data[5] = strlen(driver);
    data[6] = strlen(driver_data);
    data[7] = 0;
    memcpy(&data[8], name, strlen(name));
    memcpy(&data[8] + strlen(name), driver, strlen(driver));
    memcpy(&data[8] + strlen(name) + strlen(driver), driver_data, strlen(driver_data));
    sao_write_raw(0, data, length);
    free(data);
}

void program_diskette() {
    char magic[]       = "LIFE";
    char name[]        = "Diskette";
    char driver[]      = "badgeteam_app_link";
    char driver_data[] = "nicolai_sao";

    size_t   length = 4 + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint8_t) + strlen(name) + strlen(driver) + strlen(driver_data);
    uint8_t* data   = malloc(length);
    memcpy(data, magic, 4);
    data[4] = strlen(name);
    data[5] = strlen(driver);
    data[6] = strlen(driver_data);
    data[7] = 0;
    memcpy(&data[8], name, strlen(name));
    memcpy(&data[8] + strlen(name), driver, strlen(driver));
    memcpy(&data[8] + strlen(name) + strlen(driver), driver_data, strlen(driver_data));
    sao_write_raw(0, data, length);
    free(data);
}

static void program_sao(uint8_t type) {
    switch (type) {
        case 0:
            program_googly();
            break;
        case 1:
            program_cloud();
            break;
        case 2:
            program_casette();
            break;
        case 3:
            program_diskette();
            break;
    }
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
        if (strncmp(sao->driver, "badgeteam_app_link", strlen("badgeteam_app_link")) == 0) {
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 40, "This SAO wants to start an app:");
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 60, (char*) sao->driver_data);
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "🅰️ Start app 🅱 back ⤓ Program");
        } else {
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 40, "SAO with unsupported driver");
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 55, sao->driver);
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "🅱 back ⤓ Program");
        }
    } else if (sao->type == SAO_JSON) {
        render_header(pax_buffer, 0, 0, pax_buffer->width, 34, 18, 0xFF491d88, 0xFF43b5a0, NULL, "JSON SAO");
        pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 40, "SAO with JSON data detected\nThis type of metadata is not supported");
        pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "🅱 back ⤓ Program");
    } else {
        render_header(pax_buffer, 0, 0, pax_buffer->width, 18, 18, 0xFF491d88, 0xFF43b5a0, NULL, "Unformatted SAO");
        pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "🅱 back ⤓ Program");
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
            // Update casette eye SAOs with old data
            if (memcmp(sao.driver, "hatchery", strlen("hatchery")) == 0) {
                if (memcmp(sao.name, "cassette", strlen("cassette")) == 0) {
                    program_casette();
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
            if (memcmp(sao.driver, "storage", strlen("storage")) == 0) {
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
                    case RP2040_INPUT_JOYSTICK_LEFT:
                        program_sao(0);
                        break;
                    case RP2040_INPUT_JOYSTICK_UP:
                        program_sao(1);
                        break;
                    case RP2040_INPUT_JOYSTICK_RIGHT:
                        program_sao(2);
                        break;
                    case RP2040_INPUT_JOYSTICK_DOWN:
                        program_sao(3);
                        break;
                    case RP2040_INPUT_BUTTON_HOME:
                    case RP2040_INPUT_BUTTON_BACK:
                        if (value) {
                            exit = true;
                        }
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
