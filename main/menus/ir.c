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
#include "menu.h"
#include "rp2040.h"
#include "ir.h"
#include "hardware.h"


#define IR_CMD_BRIGHTNESS_UP 0x01
#define IR_CMD_BRIGHTNESS_DOWN 0x27
#define IR_CMD_OFF 0x56
#define IR_CMD_ON 0x51
#define IR_CMD_RED 0x08
#define IR_CMD_GREEN 0x18
#define IR_CMD_BLUE 0x60
#define IR_CMD_WHITE 0x45
#define IR_CMD_ORANGE 0x31
#define IR_CMD_LIGHT_GREEN 0x34
#define IR_CMD_LIGHT_BLUE 0x03
#define IR_CMD_TIMER_1HOUR 0x15
#define IR_CMD_YELLOW 0x32
#define IR_CMD_ALTERNATE_GREEN 0x33
#define IR_CMD_PURPLE 0x22
#define IR_CMD_TIMER_2HOURS 0x24
#define IR_CMD_SPEED_INCREASE 0x41
#define IR_CMD_MODE 0x30
#define IR_CMD_SPEED_DECREASE 0x17
#define IR_CMD_NIGHT 0x04
#define IR_CMD_MUSIC1 0x16
#define IR_CMD_MUSIC2 0x25
#define IR_CMD_MUSIC3 0x46
#define IR_CMD_MUSIC4 0x42


static void render(pax_buf_t* pax_buffer) {
    const pax_font_t *font = pax_get_font("saira regular");
    pax_background(pax_buffer, 0xFFFFFF);
    pax_noclip(pax_buffer);
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 5, "IR remote for deco lights\nA: ON, B: OFF\nLeft: R, up: G, right: B, down: W\nJoystick press: music mode\nMenu: mode\nSelect / start: speed -/+\n\nHome: exit");
}

static void send_ir(uint8_t command) {
    RP2040* rp2040 = get_rp2040();
    rp2040_ir_send(rp2040, 0xD5D5, command);
}

static void send_ir_repeated(uint8_t command) {
    for (uint8_t i = 0; i < 4; i++) {
        send_ir(command);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void menu_ir(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    bool exit = false;
    while (!exit) {
        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(buttonQueue, &buttonMessage, 1000 / portTICK_PERIOD_MS) == pdTRUE) {
            uint8_t pin = buttonMessage.input;
            bool value = buttonMessage.state;
            if (value) {
                switch(pin) {
                    case RP2040_INPUT_JOYSTICK_LEFT:
                        send_ir_repeated(IR_CMD_RED);
                        break;
                    case RP2040_INPUT_JOYSTICK_UP:
                        send_ir_repeated(IR_CMD_GREEN);
                        break;
                    case RP2040_INPUT_JOYSTICK_RIGHT:
                        send_ir_repeated(IR_CMD_BLUE);
                        break;
                    case RP2040_INPUT_JOYSTICK_DOWN:
                        send_ir_repeated(IR_CMD_WHITE);
                        break;
                    case RP2040_INPUT_JOYSTICK_PRESS:
                        send_ir_repeated(IR_CMD_MUSIC3);
                        break;
                    case RP2040_INPUT_BUTTON_ACCEPT:
                        send_ir_repeated(IR_CMD_ON);
                        break;
                    case RP2040_INPUT_BUTTON_BACK:
                        send_ir_repeated(IR_CMD_OFF);
                        break;
                    case RP2040_INPUT_BUTTON_MENU:
                        send_ir_repeated(IR_CMD_MODE);
                        break;
                    case RP2040_INPUT_BUTTON_SELECT:
                        send_ir(IR_CMD_SPEED_DECREASE);
                        break;
                    case RP2040_INPUT_BUTTON_START:
                        send_ir(IR_CMD_SPEED_INCREASE);
                        break;
                    case RP2040_INPUT_BUTTON_HOME:
                            exit = true;
                        break;
                    default:
                        break;
                }
            }
        }

        render(pax_buffer);
        ili9341_write(ili9341, pax_buffer->buf);
    }
}
