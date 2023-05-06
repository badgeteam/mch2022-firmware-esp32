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

void program_small() {
    sao_driver_storage_data_t data = {.flags         = 0,
                                      .address       = 0x50,
                                      .size_exp      = 11,  // 2 kbit (2^11)
                                      .page_size_exp = 4,   // 16 bytes (2^4)
                                      .data_offset   = 4,   // 4 pages (64 bytes)
                                      .reserved      = 0};
    sao_format("Generic small EEPROM (2kb)", SAO_DRIVER_STORAGE_NAME, (uint8_t*) &data, sizeof(data), NULL, NULL, 0, NULL, NULL, 0, true);
}

void program_googly() {
    sao_driver_storage_data_t data = {.flags         = 0,
                                      .address       = 0x50,
                                      .size_exp      = 15,  // 32 kbit (2^15)
                                      .page_size_exp = 6,   // 64 bytes (2^6)
                                      .data_offset   = 1,   // 1 page (64 bytes)
                                      .reserved      = 0};
    sao_format("Googly eye", SAO_DRIVER_STORAGE_NAME, (uint8_t*) &data, sizeof(data), NULL, NULL, 0, NULL, NULL, 0, false);
}

void program_cloud() {
    sao_driver_storage_data_t data = {.flags         = 0,
                                      .address       = 0x50,
                                      .size_exp      = 15,  // 32 kbit (2^15)
                                      .page_size_exp = 6,   // 64 bytes (2^6)
                                      .data_offset   = 1,   // 1 page (64 bytes)
                                      .reserved      = 0};

    sao_driver_basic_io_data_t data_basic_io = {
        .io1_function = SAO_DRIVER_BASIC_IO_FUNC_LED_BLUE, .io2_function = SAO_DRIVER_BASIC_IO_FUNC_LED_BLUE, .reserved = 0};

    sao_format("Cloud", SAO_DRIVER_STORAGE_NAME, (uint8_t*) &data, sizeof(data), SAO_DRIVER_BASIC_IO_NAME, (uint8_t*) &data_basic_io,
               sizeof(sao_driver_basic_io_data_t), NULL, NULL, 0, false);
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
               sizeof(data_storage), NULL, NULL, 0, false);
}

void program_cloud_tilde_small() {
    sao_driver_neopixel_data_t data_neopixel = {.length      = 2,
                                                .color_order = SAO_DRIVER_NEOPIXEL_COLOR_ORDER_GRB,  // WS2812
                                                .reserved    = 0};

    sao_driver_storage_data_t data_storage = {.flags         = 0,
                                              .address       = 0x50,
                                              .size_exp      = 11,  // 2 kbit (2^11)
                                              .page_size_exp = 4,   // 16 bytes (2^4)
                                              .data_offset   = 4,   // 4 pages (64 bytes)
                                              .reserved      = 0};

    sao_format("Cloud", SAO_DRIVER_NEOPIXEL_NAME, (uint8_t*) &data_neopixel, sizeof(data_neopixel), SAO_DRIVER_STORAGE_NAME, (uint8_t*) &data_storage,
               sizeof(data_storage), NULL, NULL, 0, true);
}

void program_cassette() {
    sao_driver_storage_data_t data = {.flags         = 0,
                                      .address       = 0x50,
                                      .size_exp      = 15,  // 32 kbit (2^15)
                                      .page_size_exp = 6,   // 64 bytes (2^6)
                                      .data_offset   = 1,   // 1 page (64 bytes)
                                      .reserved      = 0};

    sao_driver_basic_io_data_t data_basic_io = {
        .io1_function = SAO_DRIVER_BASIC_IO_FUNC_LED_RED, .io2_function = SAO_DRIVER_BASIC_IO_FUNC_LED_RED, .reserved = 0};

    sao_format("Cassette", SAO_DRIVER_STORAGE_NAME, (uint8_t*) &data, sizeof(data), SAO_DRIVER_BASIC_IO_NAME, (uint8_t*) &data_basic_io,
               sizeof(sao_driver_basic_io_data_t), NULL, NULL, 0, false);
}

void program_diskette() {
    sao_driver_storage_data_t data = {.flags         = 0,
                                      .address       = 0x50,
                                      .size_exp      = 15,  // 32 kbit (2^15)
                                      .page_size_exp = 6,   // 64 bytes (2^6)
                                      .data_offset   = 1,   // 1 page (64 bytes)
                                      .reserved      = 0};

    sao_driver_basic_io_data_t data_basic_io = {
        .io1_function = SAO_DRIVER_BASIC_IO_FUNC_LED_RED, .io2_function = SAO_DRIVER_BASIC_IO_FUNC_LED_GREEN, .reserved = 0};

    sao_format("Diskette", SAO_DRIVER_STORAGE_NAME, (uint8_t*) &data, sizeof(data), SAO_DRIVER_BASIC_IO_NAME, (uint8_t*) &data_basic_io,
               sizeof(sao_driver_basic_io_data_t), NULL, NULL, 0, false);
}

void program_ssd1306() {
    sao_driver_ssd1306_data_t data_ssd1306 = {.address = 0x3C, .height = 64, .reserved = 0};

    sao_driver_storage_data_t data_storage = {.flags         = 0,
                                              .address       = 0x50,
                                              .size_exp      = 15,  // 32 kbit (2^15)
                                              .page_size_exp = 6,   // 64 bytes (2^6)
                                              .data_offset   = 1,   // 1 page (64 bytes)
                                              .reserved      = 0};

    sao_driver_basic_io_data_t data_basic_io = {
        .io1_function = SAO_DRIVER_BASIC_IO_FUNC_LED_BLUE, .io2_function = SAO_DRIVER_BASIC_IO_FUNC_LED_RED, .reserved = 0};

    sao_format("OLED display", SAO_DRIVER_SSD1306_NAME, (uint8_t*) &data_ssd1306, sizeof(data_ssd1306), SAO_DRIVER_STORAGE_NAME, (uint8_t*) &data_storage,
               sizeof(data_storage), SAO_DRIVER_BASIC_IO_NAME, (uint8_t*) &data_basic_io, sizeof(sao_driver_basic_io_data_t), false);
}

void program_ntag() {
    sao_driver_ntag_data_t data_ntag = {.address       = 0x55,
                                        .size_exp      = 10,  // 1k (NT3H2111)
                                        .interrupt_pin = 2,   // Interrupt pin is on IO2
                                        .reserved      = 0};

    sao_driver_storage_data_t data_storage = {.flags         = 0,
                                              .address       = 0x50,
                                              .size_exp      = 15,  // 32 kbit (2^15)
                                              .page_size_exp = 6,   // 64 bytes (2^6)
                                              .data_offset   = 1,   // 1 page (64 bytes)
                                              .reserved      = 0};

    sao_driver_basic_io_data_t data_basic_io = {.io1_function = SAO_DRIVER_BASIC_IO_FUNC_LED_WHITE,
                                                .io2_function = SAO_DRIVER_BASIC_IO_FUNC_NONE,  // Used by the NTAG driver
                                                .reserved     = 0};

    sao_format("NFC tag", SAO_DRIVER_NTAG_NAME, (uint8_t*) &data_ntag, sizeof(data_ntag), SAO_DRIVER_STORAGE_NAME, (uint8_t*) &data_storage,
               sizeof(data_storage), SAO_DRIVER_BASIC_IO_NAME, (uint8_t*) &data_basic_io, sizeof(sao_driver_basic_io_data_t), false);
}

void program_tilde_butterfly() {
    sao_driver_neopixel_data_t data_neopixel = {.length = 5, .color_order = SAO_DRIVER_NEOPIXEL_COLOR_ORDER_GRB, .reserved = 0};

    sao_driver_storage_data_t data_storage = {.flags         = 0,
                                              .address       = 0x50,
                                              .size_exp      = 11,  // 2 kbit (2^11)
                                              .page_size_exp = 4,   // 16 bytes (2^4)
                                              .data_offset   = 4,   // 4 pages (64 bytes)
                                              .reserved      = 0};

    sao_format("[~] MCH butterfly", SAO_DRIVER_NEOPIXEL_NAME, (uint8_t*) &data_neopixel, sizeof(data_neopixel), SAO_DRIVER_STORAGE_NAME,
               (uint8_t*) &data_storage, sizeof(data_storage), NULL, NULL, 0, true);
}

typedef enum action {
    ACTION_NONE = 0,
    ACTION_BACK,
    ACTION_GOOGLY,
    ACTION_CLOUD,
    ACTION_CLOUD_TILDE,
    ACTION_CLOUD_TILDE_SMALL,
    ACTION_CASSETTE,
    ACTION_DISKETTE,
    ACTION_SSD1306,
    ACTION_NTAG,
    ACTION_SMALL,
    ACTION_BUTTERFLY
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

    menu_insert_item(menu, "Googly eye 32kb", NULL, (void*) ACTION_GOOGLY, -1);
    menu_insert_item(menu, "Cloud (LEDs) 32kb", NULL, (void*) ACTION_CLOUD, -1);
    menu_insert_item(menu, "Cloud (neopixels) 32kb", NULL, (void*) ACTION_CLOUD_TILDE, -1);
    menu_insert_item(menu, "Cloud (neopixels) 2kb", NULL, (void*) ACTION_CLOUD_TILDE_SMALL, -1);
    menu_insert_item(menu, "Cassette 32kb", NULL, (void*) ACTION_CASSETTE, -1);
    menu_insert_item(menu, "Diskette 32kb", NULL, (void*) ACTION_DISKETTE, -1);
    menu_insert_item(menu, "SSD1306", NULL, (void*) ACTION_SSD1306, -1);
    menu_insert_item(menu, "NTAG", NULL, (void*) ACTION_NTAG, -1);
    menu_insert_item(menu, "Generic 2kb EEPROM", NULL, (void*) ACTION_SMALL, -1);
    menu_insert_item(menu, "Tilde butterfly", NULL, (void*) ACTION_BUTTERFLY, -1);

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
            } else if (action == ACTION_CLOUD_TILDE_SMALL) {
                program_cloud_tilde_small();
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
            } else if (action == ACTION_SMALL) {
                program_small();
                break;
            } else if (action == ACTION_BUTTERFLY) {
                program_tilde_butterfly();
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

static int intPow(int x, int n) {
    int number = 1;
    for (int i = 0; i < n; ++i) number *= x;
    return number;
}

const char neopixel_color_order_strings[][5] = {"RGB",  "RBG",  "GRB",  "GBR",  "BRG",  "BGR",  "WRGB", "WRBG", "WGRB", "WGBR",
                                                "WBRG", "WBGR", "RWGB", "RWBG", "RGWB", "RGBW", "RBWG", "RBGW", "GWRB", "GWBR",
                                                "GRWB", "GRBW", "GBWR", "GBRW", "BWRG", "BWGR", "BRWG", "BRGW", "BGWR", "BGRW"};

static const char* neopixelColorOrderToString(uint8_t color_order) {
    if (color_order > SAO_DRIVER_NEOPIXEL_COLOR_ORDER_MAX) return "unknown";
    return neopixel_color_order_strings[color_order];
}

static bool neopixelColorHasWhite(uint8_t color_order) {
    if (color_order > SAO_DRIVER_NEOPIXEL_COLOR_ORDER_WRGB) return true;
    return false;
}

static const char* ntagInterruptToString(uint8_t interrupt_pin) {
    if (interrupt_pin == 0) return "none";
    if (interrupt_pin == 1) return "IO1";
    if (interrupt_pin == 2) return "IO2";
    return "unknown";
}

static const char* basicioFunctionToString(uint8_t function) {
    if (function == SAO_DRIVER_BASIC_IO_FUNC_NONE) return "unused";
    if (function == SAO_DRIVER_BASIC_IO_FUNC_LED) return "LED";
    if (function == SAO_DRIVER_BASIC_IO_FUNC_BUTTON) return "button";
    if (function == SAO_DRIVER_BASIC_IO_FUNC_LED_RED) return "red LED";
    if (function == SAO_DRIVER_BASIC_IO_FUNC_LED_GREEN) return "green LED";
    if (function == SAO_DRIVER_BASIC_IO_FUNC_LED_BLUE) return "blue LED";
    if (function == SAO_DRIVER_BASIC_IO_FUNC_LED_YELLOW) return "yellow LED";
    if (function == SAO_DRIVER_BASIC_IO_FUNC_LED_AMBER) return "amber LED";
    if (function == SAO_DRIVER_BASIC_IO_FUNC_LED_WHITE) return "white LED";
    return "unknown";
}

static bool basicioFunctionIsLED(uint8_t function) {
    if (function == SAO_DRIVER_BASIC_IO_FUNC_LED) return true;
    if (function == SAO_DRIVER_BASIC_IO_FUNC_LED_RED) return true;
    if (function == SAO_DRIVER_BASIC_IO_FUNC_LED_GREEN) return true;
    if (function == SAO_DRIVER_BASIC_IO_FUNC_LED_BLUE) return true;
    if (function == SAO_DRIVER_BASIC_IO_FUNC_LED_YELLOW) return true;
    if (function == SAO_DRIVER_BASIC_IO_FUNC_LED_AMBER) return true;
    if (function == SAO_DRIVER_BASIC_IO_FUNC_LED_WHITE) return true;
    return false;
}

bool    io1_is_led      = false;
bool    io2_is_led      = false;
uint8_t neopixel_length = 0;
bool    neopixel_white  = false;

static void render_sao_status(pax_buf_t* pax_buffer, SAO* sao) {
    char              stringbuf[256] = {0};
    const pax_font_t* font           = pax_font_saira_regular;
    pax_background(pax_buffer, 0xFFFFFF);
    pax_noclip(pax_buffer);

    io1_is_led      = false;
    io2_is_led      = false;
    neopixel_length = 0;
    neopixel_white  = false;

    if (sao->type == SAO_BINARY) {
        bool can_start_app = false;
        render_header(pax_buffer, 0, 0, pax_buffer->width, 34, 18, 0xFF491d88, 0xFF43b5a0, NULL, sao->name);
        for (uint8_t driver_index = 0; driver_index < sao->amount_of_drivers; driver_index++) {
            if (strncmp(sao->drivers[driver_index].name, SAO_DRIVER_APP_NAME, strlen(SAO_DRIVER_APP_NAME)) == 0) {
                can_start_app = true;
                snprintf(stringbuf, sizeof(stringbuf) - 1, "App: %s", (char*) sao->drivers[driver_index].data);
            } else if (strncmp(sao->drivers[driver_index].name, SAO_DRIVER_STORAGE_NAME, strlen(SAO_DRIVER_STORAGE_NAME)) == 0) {
                sao_driver_storage_data_t* data = (sao_driver_storage_data_t*) sao->drivers[driver_index].data;
                snprintf(stringbuf, sizeof(stringbuf) - 1, "%ukb EEPROM @0x%02x p=%u o=%u", intPow(2, data->size_exp) / 1024, data->address,
                         intPow(2, data->page_size_exp), data->data_offset);
            } else if (strncmp(sao->drivers[driver_index].name, SAO_DRIVER_NEOPIXEL_NAME, strlen(SAO_DRIVER_NEOPIXEL_NAME)) == 0) {
                sao_driver_neopixel_data_t* data = (sao_driver_neopixel_data_t*) sao->drivers[driver_index].data;
                snprintf(stringbuf, sizeof(stringbuf) - 1, "%u Neopixel LEDs (%s)", data->length, neopixelColorOrderToString(data->color_order));
                neopixel_length = data->length;
                neopixel_white  = neopixelColorHasWhite(data->color_order);
            } else if (strncmp(sao->drivers[driver_index].name, SAO_DRIVER_SSD1306_NAME, strlen(SAO_DRIVER_SSD1306_NAME)) == 0) {
                sao_driver_ssd1306_data_t* data = (sao_driver_ssd1306_data_t*) sao->drivers[driver_index].data;
                snprintf(stringbuf, sizeof(stringbuf) - 1, "SSD1306 128x%u OLED @0x%02x", data->height, data->address);
            } else if (strncmp(sao->drivers[driver_index].name, SAO_DRIVER_NTAG_NAME, strlen(SAO_DRIVER_NTAG_NAME)) == 0) {
                sao_driver_ntag_data_t* data = (sao_driver_ntag_data_t*) sao->drivers[driver_index].data;
                snprintf(stringbuf, sizeof(stringbuf) - 1, "NTAG NFC tag @0x%02x %ukb int=%s", data->address, intPow(2, data->size_exp) / 1024,
                         ntagInterruptToString(data->interrupt_pin));
            } else if (strncmp(sao->drivers[driver_index].name, SAO_DRIVER_BASIC_IO_NAME, strlen(SAO_DRIVER_BASIC_IO_NAME)) == 0) {
                sao_driver_basic_io_data_t* data = (sao_driver_basic_io_data_t*) sao->drivers[driver_index].data;
                snprintf(stringbuf, sizeof(stringbuf) - 1, "Basic I/O: %s and %s", basicioFunctionToString(data->io1_function),
                         basicioFunctionToString(data->io2_function));
                io1_is_led = basicioFunctionIsLED(data->io1_function);
                io2_is_led = basicioFunctionIsLED(data->io2_function);
            } else {
                snprintf(stringbuf, sizeof(stringbuf) - 1, "Unsupported driver \"%s\"", sao->drivers[driver_index].name);
            }
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 40 + (driver_index * 18), stringbuf);
        }
        pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, can_start_app ? "🅰️ Start app 🅱 back 🆂 Program" : "🅱 back 🆂 Program");
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

void reset_sao_io() {
    RP2040* rp2040 = get_rp2040();
    // rp2040_set_ws2812_mode(rp2040, 0); // FIXME: fix bug in RP2040 firmware first, then add this line
    rp2040_set_gpio_value(rp2040, 0, false);
    rp2040_set_gpio_value(rp2040, 1, false);
    rp2040_set_gpio_dir(rp2040, 0, false);
    rp2040_set_gpio_dir(rp2040, 1, false);
}

void set_sao_io(uint8_t pin, bool value) {
    printf("Set IO: %u to %u\n", pin, value);
    RP2040* rp2040 = get_rp2040();
    rp2040_set_gpio_dir(rp2040, pin, true);
    rp2040_set_gpio_value(rp2040, pin, value);
}

void set_sao_neopixel(uint32_t color) {
    if (neopixel_length > 0) {
        printf("Set neopixels to %08X\n", color);
        RP2040* rp2040 = get_rp2040();
        rp2040_set_ws2812_length(rp2040, neopixel_length);
        rp2040_set_ws2812_mode(rp2040, neopixel_white ? 2 : 1);
        for (uint8_t i = 0; i < neopixel_length; i++) {
            rp2040_set_ws2812_data(rp2040, i, color);
        }
        rp2040_ws2812_trigger(rp2040);
    }
}

void menu_sao(xQueueHandle button_queue) {
    rp2040_input_message_t buttonMessage = {0};
    while (xQueueReceive(button_queue, &buttonMessage, 0) == pdTRUE) {
        // Flush!
    }

    pax_buf_t* pax_buffer = get_pax_buffer();
    bool       exit       = false;
    bool       identify   = true;
    SAO        sao        = {0};
    while (!exit) {
        if (identify) {
            sao_identify(&sao);
            identify = false;
        }

        // Update the EEPROM contents of the SAOs Renze handed out at MCH2022
        if (sao.type == SAO_BINARY) {
            // Update cloud SAOs with old data
            if (memcmp(sao.drivers[0].name, "hatchery", strlen("hatchery")) == 0) {
                if (memcmp(sao.name, "cloud", strlen("cloud")) == 0) {
                    program_cloud();
                    continue;
                }
            }
            // Update cassette eye SAOs with old data
            if (memcmp(sao.drivers[0].name, "hatchery", strlen("hatchery")) == 0) {
                if (memcmp(sao.name, "casette", strlen("casette")) == 0) {
                    program_cassette();
                    continue;
                } else if (memcmp(sao.name, "cassette", strlen("cassette")) == 0) {
                    program_cassette();
                    continue;
                }
            }
            // Update googly eye SAOs with old data
            if (memcmp(sao.drivers[0].name, "hatchery", strlen("hatchery")) == 0) {
                if (memcmp(sao.name, "diskette", strlen("diskette")) == 0) {
                    program_diskette();
                    continue;
                }
            }
            // Update diskette SAOs with old data
            if (memcmp(sao.drivers[0].name, SAO_DRIVER_STORAGE_NAME, strlen(SAO_DRIVER_STORAGE_NAME)) == 0) {
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

        if (xQueueReceive(button_queue, &buttonMessage, 500 / portTICK_PERIOD_MS) == pdTRUE) {
            uint8_t pin   = buttonMessage.input;
            bool    value = buttonMessage.state;
            if (value) {
                switch (pin) {
                    case RP2040_INPUT_JOYSTICK_UP:
                        if (io1_is_led) {
                            set_sao_io(0, true);
                        }
                        set_sao_neopixel(0xFF000000);
                        break;
                    case RP2040_INPUT_JOYSTICK_DOWN:
                        if (io1_is_led) {
                            set_sao_io(0, false);
                        }
                        set_sao_neopixel(0x00FF0000);
                        break;
                    case RP2040_INPUT_JOYSTICK_RIGHT:
                        if (io2_is_led) {
                            set_sao_io(1, true);
                        }
                        set_sao_neopixel(0x0000FF00);
                        break;
                    case RP2040_INPUT_JOYSTICK_LEFT:
                        if (io2_is_led) {
                            set_sao_io(1, false);
                        }
                        set_sao_neopixel(0x000000FF);
                        break;
                    case RP2040_INPUT_BUTTON_START:
                        menu_sao_format(button_queue);
                        identify = true;
                        break;
                    case RP2040_INPUT_BUTTON_HOME:
                    case RP2040_INPUT_BUTTON_BACK:
                        exit = true;
                        break;
                    case RP2040_INPUT_BUTTON_ACCEPT:
                        if ((sao.type == SAO_BINARY) && (strncmp(sao.drivers[0].name, "badgeteam_app_link", strlen("badgeteam_app_link")) == 0)) {
                            if (!sao_is_app_installed((char*) sao.drivers[0].data)) {
                                if (connect_to_wifi()) {
                                    render_message("Installing app...");
                                    display_flush();
                                    if (sao_install_app(button_queue, (char*) sao.drivers[0].data)) {
                                        sao_start_app((char*) sao.drivers[0].data);
                                    } else {
                                        render_message("Failed to install app");
                                        display_flush();
                                        wait_for_button();
                                    }
                                }
                            } else {
                                render_message("Starting app...");
                                display_flush();
                                sao_start_app((char*) sao.drivers[0].data);
                            }
                        }
                        identify = true;
                        break;
                    default:
                        break;
                }
            }
        }
    }

    reset_sao_io();
}
