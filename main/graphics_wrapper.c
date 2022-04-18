#include <string.h>
#include "graphics_wrapper.h"
#include "hardware.h"
#include "pax_keyboard.h"
#include "button_wrapper.h"

void render_message(pax_buf_t *aBuffer, char* message, float aPosX, float aPosY, float aWidth, float aHeight) {
    pax_col_t fgColor = 0xFFFF0000;
    pax_col_t bgColor = 0xFFFFD4D4;
    pax_clip(aBuffer, aPosX, aPosY, aWidth, aHeight);
    pax_simple_rect(aBuffer, bgColor, aPosX, aPosY, aWidth, aHeight);
    pax_outline_rect(aBuffer, fgColor, aPosX, aPosY, aWidth, aHeight);
    pax_clip(aBuffer, aPosX + 1, aPosY + 1, aWidth - 2, aHeight - 2);
    pax_draw_text(aBuffer, fgColor, NULL, 18, aPosX + 1, aPosY + 1, message);
    pax_noclip(aBuffer);
}

esp_err_t graphics_task(pax_buf_t* pax_buffer, ILI9341* ili9341, uint8_t* framebuffer, menu_t* menu, char* message) {
    pax_background(pax_buffer, 0xCCCCCC);
    if (menu != NULL) {
        menu_render(pax_buffer, menu, 10, 10, 320-20, 240-20);
    }
    
    if (message != NULL) {
        render_message(pax_buffer, message, 20, 110, 320-40, 20);
    }

    return ili9341_write(ili9341, framebuffer);
}

bool keyboard(xQueueHandle buttonQueue, pax_buf_t* aBuffer, ILI9341* ili9341, uint8_t* framebuffer, float aPosX, float aPosY, float aWidth, float aHeight, const char* aTitle, const char* aHint, char* aOutput, size_t aOutputSize) {
    bool accepted = false;
    pkb_ctx_t kb_ctx;
    pkb_init(aBuffer, &kb_ctx, aOutput);

    pax_col_t fgColor = 0xFF000000;
    pax_col_t bgColor = 0xFFFFFFFF;
    pax_col_t shadowColor = 0xFFC0C3C8;
    pax_col_t borderColor = 0xFF0000AA;
    pax_col_t titleBgColor = 0xFF080764;
    pax_col_t titleColor = 0xFFFFFFFF;
    pax_col_t selColor = 0xff007fff;

    kb_ctx.text_col       = borderColor;
    kb_ctx.sel_text_col   = selColor;
    kb_ctx.sel_col        = selColor;
    kb_ctx.bg_col         = bgColor;

    kb_ctx.kb_font_size = 18;

    float titleHeight = 20;
    float hintHeight = 14;

    pax_noclip(aBuffer);
    pax_simple_rect(aBuffer, shadowColor, aPosX+5, aPosY+5, aWidth, aHeight);
    pax_simple_rect(aBuffer, bgColor, aPosX, aPosY, aWidth, aHeight);
    pax_outline_rect(aBuffer, borderColor, aPosX, aPosY, aWidth, aHeight);
    pax_simple_rect(aBuffer, titleBgColor, aPosX, aPosY, aWidth, titleHeight);
    pax_simple_line(aBuffer, titleColor, aPosX + 1, aPosY + titleHeight, aPosX + aWidth - 2, aPosY + titleHeight - 1);
    pax_clip(aBuffer, aPosX + 1, aPosY + 1, aWidth - 2, titleHeight - 2);
    pax_draw_text(aBuffer, titleColor, NULL, titleHeight - 2, aPosX + 1, aPosY + 1, aTitle);
    pax_clip(aBuffer, aPosX + 1, aPosY + aHeight - hintHeight, aWidth - 2, hintHeight);
    pax_draw_text(aBuffer, borderColor, NULL, hintHeight - 2, aPosX + 1, aPosY + aHeight - hintHeight, aHint);
    pax_noclip(aBuffer);

    kb_ctx.x = aPosX + 1;
    kb_ctx.y = aPosY + titleHeight + 1 ;
    kb_ctx.width = aWidth - 2;
    kb_ctx.height = aHeight - 3 - titleHeight - hintHeight;

    bool running = true;
    while (running) {
        button_message_t buttonMessage = {0};
        if (xQueueReceive(buttonQueue, &buttonMessage, 16 / portTICK_PERIOD_MS) == pdTRUE) {
            uint8_t pin = buttonMessage.button;
            bool value = buttonMessage.state;
            switch(pin) {
                case PCA9555_PIN_BTN_JOY_DOWN:
                    if (value) {
                        pkb_press(&kb_ctx, PKB_DOWN);
                    } else {
                        pkb_release(&kb_ctx, PKB_DOWN);
                    }
                    break;
                case PCA9555_PIN_BTN_JOY_UP:
                    if (value) {
                        pkb_press(&kb_ctx, PKB_UP);
                    } else {
                        pkb_release(&kb_ctx, PKB_UP);
                    }
                    break;
                case PCA9555_PIN_BTN_JOY_LEFT:
                    if (value) {
                        pkb_press(&kb_ctx, PKB_LEFT);
                    } else {
                        pkb_release(&kb_ctx, PKB_LEFT);
                    }
                    break;
                case PCA9555_PIN_BTN_JOY_RIGHT:
                    if (value) {
                        pkb_press(&kb_ctx, PKB_RIGHT);
                    } else {
                        pkb_release(&kb_ctx, PKB_RIGHT);
                    }
                    break;
                case PCA9555_PIN_BTN_JOY_PRESS:
                    if (value) {
                        pkb_press(&kb_ctx, PKB_SHIFT);
                    } else {
                        pkb_release(&kb_ctx, PKB_SHIFT);
                    }
                    break;
                case PCA9555_PIN_BTN_ACCEPT:
                    if (value) {
                        pkb_press(&kb_ctx, PKB_CHARSELECT);
                    } else {
                        pkb_release(&kb_ctx, PKB_CHARSELECT);
                    }
                    break;
                case PCA9555_PIN_BTN_BACK:
                    if (value) {
                        pkb_press(&kb_ctx, PKB_DELETE_BEFORE);
                    } else {
                        pkb_release(&kb_ctx, PKB_DELETE_BEFORE);
                    }
                    break;
                case PCA9555_PIN_BTN_SELECT:
                    if (value) {
                        pkb_press(&kb_ctx, PKB_MODESELECT);
                    } else {
                        pkb_release(&kb_ctx, PKB_MODESELECT);
                    }
                    break;
                case PCA9555_PIN_BTN_HOME:
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
            pkb_redraw(aBuffer, &kb_ctx);
            ili9341_write(ili9341, framebuffer);
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
