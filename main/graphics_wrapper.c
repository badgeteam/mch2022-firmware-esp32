#include <string.h>
#include "graphics_wrapper.h"
#include "hardware.h"
#include "pax_keyboard.h"
#include "rp2040.h"

void render_message(pax_buf_t *pax_buffer, char* message, float aPosX, float aPosY, float aWidth, float aHeight) {
    pax_col_t fgColor = 0xFFFF0000;
    pax_col_t bgColor = 0xFFFFD4D4;
    pax_clip(pax_buffer, aPosX, aPosY, aWidth, aHeight);
    pax_simple_rect(pax_buffer, bgColor, aPosX, aPosY, aWidth, aHeight);
    pax_outline_rect(pax_buffer, fgColor, aPosX, aPosY, aWidth, aHeight);
    pax_clip(pax_buffer, aPosX + 1, aPosY + 1, aWidth - 2, aHeight - 2);
    pax_draw_text(pax_buffer, fgColor, NULL, 18, aPosX + 1, aPosY + 1, message);
    pax_noclip(pax_buffer);
}

esp_err_t graphics_task(pax_buf_t* pax_buffer, ILI9341* ili9341,  menu_t* menu, char* message) {
    pax_background(pax_buffer, 0xCCCCCC);
    if (menu != NULL) {
        menu_render(pax_buffer, menu, 10, 10, 320-20, 240-20, 0xFF000000);
    }
    
    if (message != NULL) {
        render_message(pax_buffer, message, 20, 110, 320-40, 20);
    }

    return ili9341_write(ili9341, pax_buffer->buf);
}

bool keyboard(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, float aPosX, float aPosY, float aWidth, float aHeight, const char* aTitle, const char* aHint, char* aOutput, size_t aOutputSize) {
    const pax_font_t *font = pax_get_font("saira regular");
    bool accepted = false;
    pkb_ctx_t kb_ctx;
    pkb_init(pax_buffer, &kb_ctx, 32);
    pkb_set_content(&kb_ctx, aOutput);
    kb_ctx.kb_font = font;
    kb_ctx.text_font = font;

    pax_col_t fgColor = 0xFF000000;
    pax_col_t bgColor = 0xFFFFFFFF;
    pax_col_t shadowColor = 0xFFC0C3C8;
    pax_col_t borderColor = 0xFF0000AA;
    pax_col_t titleBgColor = 0xFF080764;
    pax_col_t titleColor = 0xFFFFFFFF;
    pax_col_t selColor = 0xff007fff;

    kb_ctx.text_col       = borderColor;
    kb_ctx.sel_text_col   = bgColor;
    kb_ctx.sel_col        = selColor;
    kb_ctx.bg_col         = bgColor;

    kb_ctx.kb_font_size = 18;

    float titleHeight = 20;
    float hintHeight = 14;

    pax_noclip(pax_buffer);
    pax_simple_rect(pax_buffer, shadowColor, aPosX+5, aPosY+5, aWidth, aHeight);
    pax_simple_rect(pax_buffer, bgColor, aPosX, aPosY, aWidth, aHeight);
    pax_outline_rect(pax_buffer, borderColor, aPosX, aPosY, aWidth, aHeight);
    pax_simple_rect(pax_buffer, titleBgColor, aPosX, aPosY, aWidth, titleHeight);
    pax_simple_line(pax_buffer, titleColor, aPosX + 1, aPosY + titleHeight, aPosX + aWidth - 2, aPosY + titleHeight - 1);
    pax_clip(pax_buffer, aPosX + 1, aPosY + 1, aWidth - 2, titleHeight - 2);
    pax_draw_text(pax_buffer, titleColor, font, titleHeight - 2, aPosX + 1, aPosY + 1, aTitle);
    pax_clip(pax_buffer, aPosX + 1, aPosY + aHeight - hintHeight, aWidth - 2, hintHeight);
    pax_draw_text(pax_buffer, borderColor, font, hintHeight - 2, aPosX + 1, aPosY + aHeight - hintHeight, aHint);
    pax_noclip(pax_buffer);

    kb_ctx.x = aPosX + 1;
    kb_ctx.y = aPosY + titleHeight + 1 ;
    kb_ctx.width = aWidth - 2;
    kb_ctx.height = aHeight - 3 - titleHeight - hintHeight;

    bool running = true;
    while (running) {
        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(buttonQueue, &buttonMessage, 16 / portTICK_PERIOD_MS) == pdTRUE) {
            uint8_t pin = buttonMessage.input;
            bool value = buttonMessage.state;
            switch(pin) {
                case RP2040_INPUT_JOYSTICK_DOWN:
                    if (value) {
                        pkb_press(&kb_ctx, PKB_DOWN);
                    } else {
                        pkb_release(&kb_ctx, PKB_DOWN);
                    }
                    break;
                case RP2040_INPUT_JOYSTICK_UP:
                    if (value) {
                        pkb_press(&kb_ctx, PKB_UP);
                    } else {
                        pkb_release(&kb_ctx, PKB_UP);
                    }
                    break;
                case RP2040_INPUT_JOYSTICK_LEFT:
                    if (value) {
                        pkb_press(&kb_ctx, PKB_LEFT);
                    } else {
                        pkb_release(&kb_ctx, PKB_LEFT);
                    }
                    break;
                case RP2040_INPUT_JOYSTICK_RIGHT:
                    if (value) {
                        pkb_press(&kb_ctx, PKB_RIGHT);
                    } else {
                        pkb_release(&kb_ctx, PKB_RIGHT);
                    }
                    break;
                case RP2040_INPUT_JOYSTICK_PRESS:
                    if (value) {
                        pkb_press(&kb_ctx, PKB_SHIFT);
                    } else {
                        pkb_release(&kb_ctx, PKB_SHIFT);
                    }
                    break;
                case RP2040_INPUT_BUTTON_ACCEPT:
                    if (value) {
                        pkb_press(&kb_ctx, PKB_CHARSELECT);
                    } else {
                        pkb_release(&kb_ctx, PKB_CHARSELECT);
                    }
                    break;
                case RP2040_INPUT_BUTTON_BACK:
                    if (value) {
                        pkb_press(&kb_ctx, PKB_DELETE_BEFORE);
                    } else {
                        pkb_release(&kb_ctx, PKB_DELETE_BEFORE);
                    }
                    break;
                case RP2040_INPUT_BUTTON_SELECT:
                    if (value) {
                        pkb_press(&kb_ctx, PKB_MODESELECT);
                    } else {
                        pkb_release(&kb_ctx, PKB_MODESELECT);
                    }
                    break;
                case RP2040_INPUT_BUTTON_HOME:
                    if (value) {
                        running = false;
                    }
                    break;
                default:
                    break;
            }
        }
        pkb_loop(&kb_ctx);
        if (kb_ctx.dirty) {
            pkb_redraw(pax_buffer, &kb_ctx);
            ili9341_write(ili9341, pax_buffer->buf);
        }
        if (kb_ctx.input_accepted) {
            memset(aOutput, 0, aOutputSize);
            strncpy(aOutput, kb_ctx.content, aOutputSize - 1);
            running = false;
            accepted = true;
        }
    }
    pkb_destroy(&kb_ctx);
    return accepted;
}
