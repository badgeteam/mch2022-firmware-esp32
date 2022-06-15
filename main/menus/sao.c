#include <stdio.h>
#include <string.h>
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_system.h>
#include <esp_err.h>
#include <esp_log.h>
#include "appfs.h"
#include "ili9341.h"
#include "pax_gfx.h"
#include "pax_codecs.h"
#include "menu.h"
#include "rp2040.h"
#include "launcher.h"
#include "settings.h"
#include "sao.h"
#include "sao_eeprom.h"

static uint8_t testdata[] = {
    0x4c, 0x49, 0x46, 0x45, 0x10, 0x0a, 0x04, 0x00,  0x4d, 0x79, 0x20, 0x61, 0x6d, 0x61, 0x7a, 0x69,
    0x6e, 0x67, 0x20, 0x61, 0x64, 0x64, 0x6f, 0x6e,  0x61, 0x6d, 0x61, 0x7a, 0x69, 0x6e, 0x67, 0x2e,
    0x70, 0x79, 0x01, 0xff, 0xff, 0xff};

static uint8_t cloud[] = {
    0x4c, 0x49, 0x46, 0x45, 0x05, 0x08, 0x04, 0x00,  0x63, 0x6C, 0x6F, 0x75, 0x64, 0x68, 0x61, 0x74,
    0x63, 0x68, 0x65, 0x72, 0x79, 0x00, 0x50, 0x08,  0x07
};

static uint8_t cassette[] = {
    0x4c, 0x49, 0x46, 0x45, 0x08, 0x08, 0x04, 0x00,  0x63, 0x61, 0x73, 0x73, 0x65, 0x74, 0x74, 0x65,
    0x68, 0x61, 0x74, 0x63, 0x68, 0x65, 0x72, 0x79, 0x00, 0x50, 0x08,  0x07
};

static uint8_t diskette[] = {
    0x4c, 0x49, 0x46, 0x45, 0x08, 0x08, 0x04, 0x00,  0x64, 0x69, 0x73, 0x6B, 0x65, 0x74, 0x74, 0x65,
    0x68, 0x61, 0x74, 0x63, 0x68, 0x65, 0x72, 0x79, 0x00, 0x50, 0x08,  0x07
};

static void program_sao(uint8_t type) {
    switch (type) {
        case 0:
            sao_write_raw(0, testdata, sizeof(testdata));
            break;
        case 1:
            sao_write_raw(0, cloud, sizeof(cloud));
            break;
        case 2:
            sao_write_raw(0, cassette, sizeof(cassette));
            break;
        case 3:
            sao_write_raw(0, diskette, sizeof(diskette));
            break;
    }
}

static void render_sao_status(pax_buf_t* pax_buffer, SAO* sao) {
    const pax_font_t *font = pax_get_font("saira regular");
    pax_background(pax_buffer, 0xFFFFFF);
    pax_noclip(pax_buffer);
    char buffer[64];
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 0, "SAO information");
    snprintf(buffer, sizeof(buffer), "Type: %s", (sao->type == SAO_BINARY) ? "binary data" : (sao->type == SAO_JSON) ? "json data" : (sao->type == SAO_UNFORMATTED) ? "unformatted" : "unknown");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 20, buffer);
    snprintf(buffer, sizeof(buffer), "Name: %s", (sao->name != NULL) ? sao->name : "----");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 40, buffer);
    snprintf(buffer, sizeof(buffer), "Driver: %s", (sao->driver != NULL) ? sao->driver : "----");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 60, buffer);
    /*snprintf(buffer, sizeof(buffer), "Driver data: ");
    for (uint8_t index = 0; index < sao->driver_data_length; index++) {
        char tmp[128];
        snprintf(tmp, sizeof(tmp), "%s ", buffer);//%02X, (uint8_t) sao->driver_data[index]);
        strncpy(buffer, tmp, sizeof(buffer));
    }
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 80, buffer);*/
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "[B] back [Joystick] Program");
}

static void render_sao_not_detected(pax_buf_t* pax_buffer) {
    const pax_font_t *font = pax_get_font("saira regular");
    pax_background(pax_buffer, 0xFFAAAA);
    pax_noclip(pax_buffer);
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 0, "No SAO detected");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "[B] back");
}

void menu_sao(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    bool exit = false;
    while (!exit) {
        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(buttonQueue, &buttonMessage, 200 / portTICK_PERIOD_MS) == pdTRUE) {
            uint8_t pin = buttonMessage.input;
            bool value = buttonMessage.state;
            switch(pin) {
                case RP2040_INPUT_JOYSTICK_LEFT:
                    if (value) {
                        program_sao(0);
                    }
                    break;
                case RP2040_INPUT_JOYSTICK_UP:
                    if (value) {
                        program_sao(1);
                    }
                    break;
                case RP2040_INPUT_JOYSTICK_RIGHT:
                    if (value) {
                        program_sao(2);
                    }
                    break;
                case RP2040_INPUT_JOYSTICK_DOWN:
                    if (value) {
                        program_sao(3);
                    }
                    break;
                case RP2040_INPUT_BUTTON_HOME:
                case RP2040_INPUT_BUTTON_BACK:
                    if (value) {
                        exit = true;
                    }
                    break;
                default:
                    break;
            }
        }

        SAO* sao = sao_identify();
        if (sao != NULL) {
            render_sao_status(pax_buffer, sao);
            sao_free(sao);
        } else {
            render_sao_not_detected(pax_buffer);
        }
        ili9341_write(ili9341, pax_buffer->buf);
    }
}
