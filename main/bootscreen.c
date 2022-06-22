#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "ili9341.h"
#include "pax_codecs.h"
#include "pax_gfx.h"

extern const uint8_t mch2022_logo_png_start[] asm("_binary_mch2022_logo_png_start");
extern const uint8_t mch2022_logo_png_end[] asm("_binary_mch2022_logo_png_end");

void display_boot_screen(pax_buf_t* pax_buffer, ILI9341* ili9341, const char* text) {
    const pax_font_t* font = pax_get_font("saira regular");

    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0xFFFFFF);
    float x = (320 / 2) - (212 / 2);
    float y = ((240 - 32 - 10) / 2) - (160 / 2);
    pax_insert_png_buf(pax_buffer, mch2022_logo_png_start, mch2022_logo_png_end - mch2022_logo_png_start, x, y, 0);

    pax_vec1_t size = pax_text_size(font, 18, text);
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, (320 / 2) - (size.x / 2), 240 - 32, text);
    ili9341_write(ili9341, pax_buffer->buf);
}
