#include "graphics_wrapper.h"

void message_render(pax_buf_t *aBuffer, char* message, float aPosX, float aPosY, float aWidth, float aHeight) {
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
        message_render(pax_buffer, message, 20, 110, 320-40, 20);
    }

    return ili9341_write(ili9341, framebuffer);
}
