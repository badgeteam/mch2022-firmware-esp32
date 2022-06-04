#include <stdio.h>
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "ili9341.h"
#include "pax_gfx.h"
#include "rp2040.h"

void test_buttons(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    bool render = true;
    bool quit = false;

    bool btn_joy_down = false;
    bool btn_joy_up = false;
    bool btn_joy_left = false;
    bool btn_joy_right = false;
    bool btn_joy_press = false;
    bool btn_home = false;
    bool btn_menu = false;
    bool btn_select = false;
    bool btn_start = false;
    bool btn_accept = false;
    bool btn_back = false;
    
    bool btn_joy_down_green = false;
    bool btn_joy_up_green = false;
    bool btn_joy_left_green = false;
    bool btn_joy_right_green = false;
    bool btn_joy_press_green = false;
    bool btn_home_green = false;
    bool btn_menu_green = false;
    bool btn_select_green = false;
    bool btn_start_green = false;
    bool btn_accept_green = false;
    bool btn_back_green = false;
    

    while (!quit) {
        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(buttonQueue, &buttonMessage, 16 / portTICK_PERIOD_MS) == pdTRUE) {
            uint8_t pin = buttonMessage.input;
            bool value = buttonMessage.state;
            render = true;
            switch(pin) {
                case RP2040_INPUT_JOYSTICK_DOWN:
                    btn_joy_down = value;
                    if (value) btn_joy_down_green = true;
                    break;
                case RP2040_INPUT_JOYSTICK_UP:
                    btn_joy_up = value;
                    if (value) btn_joy_up_green = true;
                    break;
                case RP2040_INPUT_JOYSTICK_LEFT:
                    btn_joy_left = value;
                    if (value) btn_joy_left_green = true;
                    break;
                case RP2040_INPUT_JOYSTICK_RIGHT:
                    btn_joy_right = value;
                    if (value) btn_joy_right_green = true;
                    break;
                case RP2040_INPUT_JOYSTICK_PRESS:
                    btn_joy_press = value;
                    if (value) btn_joy_press_green = true;
                    break;
                case RP2040_INPUT_BUTTON_HOME:
                    btn_home = value;
                    if (value) btn_home_green = true;
                    break;
                case RP2040_INPUT_BUTTON_MENU:
                    btn_menu = value;
                    if (value) btn_menu_green = true;
                    break;
                case RP2040_INPUT_BUTTON_SELECT:
                    btn_select = value;
                    if (value) btn_select_green = true;
                    break;
                case RP2040_INPUT_BUTTON_START:
                    btn_start = value;
                    if (value) btn_start_green = true;
                    break;
                case RP2040_INPUT_BUTTON_ACCEPT:
                    btn_accept = value;
                    if (value) btn_accept_green = true;
                    break;
                case RP2040_INPUT_BUTTON_BACK:
                    btn_back = value;
                    if (value) btn_back_green = true;
                default:
                    break;
            }
        }

        if (render) {
            pax_noclip(pax_buffer);
            pax_background(pax_buffer, 0x325aa8);
            pax_draw_text(pax_buffer, 0xFFFFFFFF, NULL, 18, 0, 20*0, "Press HOME + START to exit");
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "JOY DOWN   %s", btn_joy_down ? "PRESSED" : "released");
            pax_draw_text(pax_buffer, btn_joy_down_green ? 0xFF00FF00 : 0xFFFFFFFF, NULL, 18, 0, 20*1, buffer);
            snprintf(buffer, sizeof(buffer), "JOY UP     %s", btn_joy_up ? "PRESSED" : "released");
            pax_draw_text(pax_buffer, btn_joy_up_green ? 0xFF00FF00 : 0xFFFFFFFF, NULL, 18, 0, 20*2, buffer);
            snprintf(buffer, sizeof(buffer), "JOY LEFT   %s", btn_joy_left ? "PRESSED" : "released");
            pax_draw_text(pax_buffer, btn_joy_right_green ? 0xFF00FF00 : 0xFFFFFFFF, NULL, 18, 0, 20*3, buffer);
            snprintf(buffer, sizeof(buffer), "JOY RIGHT  %s", btn_joy_right ? "PRESSED" : "released");
            pax_draw_text(pax_buffer, btn_joy_left_green ? 0xFF00FF00 : 0xFFFFFFFF, NULL, 18, 0, 20*4, buffer);
            snprintf(buffer, sizeof(buffer), "JOY PRESS  %s", btn_joy_press ? "PRESSED" : "released");
            pax_draw_text(pax_buffer, btn_joy_press_green ? 0xFF00FF00 : 0xFFFFFFFF, NULL, 18, 0, 20*5, buffer);
            snprintf(buffer, sizeof(buffer), "BTN HOME   %s", btn_home ? "PRESSED" : "released");
            pax_draw_text(pax_buffer, btn_home_green ? 0xFF00FF00 : 0xFFFFFFFF, NULL, 18, 0, 20*6, buffer);
            snprintf(buffer, sizeof(buffer), "BTN MENU   %s", btn_menu ? "PRESSED" : "released");
            pax_draw_text(pax_buffer, btn_menu_green ? 0xFF00FF00 : 0xFFFFFFFF, NULL, 18, 0, 20*7, buffer);
            snprintf(buffer, sizeof(buffer), "BTN SELECT %s", btn_select ? "PRESSED" : "released");
            pax_draw_text(pax_buffer, btn_select_green ? 0xFF00FF00 : 0xFFFFFFFF, NULL, 18, 0, 20*8, buffer);
            snprintf(buffer, sizeof(buffer), "BTN START  %s", btn_start ? "PRESSED" : "released");
            pax_draw_text(pax_buffer, btn_start_green ? 0xFF00FF00 : 0xFFFFFFFF, NULL, 18, 0, 20*9, buffer);
            snprintf(buffer, sizeof(buffer), "BTN A      %s", btn_accept ? "PRESSED" : "released");
            pax_draw_text(pax_buffer, btn_accept_green ? 0xFF00FF00 : 0xFFFFFFFF, NULL, 18, 0, 20*10, buffer);
            snprintf(buffer, sizeof(buffer), "BTN B      %s", btn_back ? "PRESSED" : "released");
            pax_draw_text(pax_buffer, btn_back_green ? 0xFF00FF00 : 0xFFFFFFFF, NULL, 18, 0, 20*11, buffer);
            ili9341_write(ili9341, pax_buffer->buf);
            render = false;
        }

        if (btn_home && btn_start) {
            quit = true;
        }
    }
}

