/*
* BADGE.TEAM framebuffer driver
* Uses parts of the Adafruit GFX Arduino libray
*/

/*
This is the core graphics library for all our displays, providing a common
set of graphics primitives (points, lines, circles, etc.).  It needs to be
paired with a hardware-specific library for each display device we carry
(to handle the lower-level functions).

Adafruit invests time and resources providing this open source code, please
support Adafruit & open-source hardware by purchasing products from Adafruit!

Copyright (c) 2013 Adafruit Industries.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/

#include "include/driver_framebuffer_internal.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#define TAG "fb-text"

// Draws a character to the screen
void _print_char(Window* window, unsigned char c, int16_t x0, int16_t y0, uint8_t xScale, uint8_t yScale, uint32_t color, const GFXfont *font) {
    if ((c < font->first) || (c > font->last)) {
        ESP_LOGE(TAG, "print_char called with unprintable character");
        return;
    }

    c -= (uint8_t) font->first;
    const GFXglyph *glyph   = font->glyph + c;
    const uint8_t  *bitmap  = font->bitmap;

    uint16_t bitmapOffset = glyph->bitmapOffset;
    uint8_t  width        = glyph->width;
    uint8_t  height       = glyph->height;
    int8_t   xOffset      = glyph->xOffset;
    int8_t   yOffset      = glyph->yOffset;

    uint8_t  bit = 0, bits = 0;
    for (uint8_t y = 0; y < height; y++) {
        for (uint8_t x = 0; x < width; x++) {
            if(!(bit++ & 7)) bits = bitmap[bitmapOffset++];
            if(bits & 0x80) {
                if (xScale == 1 && yScale == 1) {
                    driver_framebuffer_setPixel(window, x0+xOffset+x, y0+yOffset+y-1, color);
                } else {
                    driver_framebuffer_rect(window, x0+(xOffset+x)*xScale, y0+(yOffset+y)*yScale-1, xScale, yScale, true, color);
                }
            }
            bits <<= 1;
        }
    }
}

// Draws a string to the screen
void _write(Window* window, uint8_t c, int16_t x0, int16_t *x, int16_t *y, uint8_t xScale, uint8_t yScale, uint32_t color, const GFXfont *font) {
    if (font == NULL) { ESP_LOGE(TAG, "write called without font"); return; }
    const GFXglyph *glyph = font->glyph + c - (uint8_t) font->first;
    if (c == '\n') {
        *x = x0;
        *y += font->yAdvance * yScale;
    } else if (c != '\r') {
        _print_char(window, c, *x, *y+(font->yAdvance*yScale), xScale, yScale, color, font);
        *x += glyph->xAdvance * xScale;
    }
}

uint16_t _char_width(uint8_t c, const GFXfont *font) {
    if (font == NULL) return 0;
    if ((c < font->first) || (c > font->last)) return 0;
    const GFXglyph *glyph = font->glyph + c - (uint8_t) font->first;
    if ((c == '\r') || (c == '\n')) return 0;
    return glyph->xAdvance;
}


uint16_t driver_framebuffer_print(Window* window, const char* str, int16_t x0, int16_t y0, uint8_t xScale, uint8_t yScale, uint32_t color, const GFXfont *font) {
    int16_t x = x0, y = y0;
    for (uint16_t i = 0; i < strlen(str); i++) {
        _write(window, str[i], x0, &x, &y, xScale, yScale, color, font);
    }
    return y;
}

uint16_t driver_framebuffer_print_len(Window* window, const char* str, int16_t len, int16_t x0, int16_t y0, uint8_t xScale, uint8_t yScale, uint32_t color, const GFXfont *font) {
    int16_t x = x0, y = y0;
    for (uint16_t i = 0; i < len; i++) {
        _write(window, str[i], x0, &x, &y, xScale, yScale, color, font);
    }
    return y;
}

uint16_t driver_framebuffer_get_string_width(const char* str, const GFXfont *font) {
    uint16_t width = 0;
    uint16_t maxWidth = 0;
    for (uint16_t i = 0; i < strlen(str); i++) {
        if (str[i] == '\n') {
            if (maxWidth < width) maxWidth = width;
            width = 0;
        } else {
            width += _char_width(str[i], font);
        }
    }
    if (maxWidth > width) width = maxWidth;
    return width;
}

uint16_t driver_framebuffer_get_string_height(const char* str, const GFXfont *font) {
    uint16_t height = font->yAdvance;
    if (strlen(str) < 1) return 0;
    for (uint16_t i = 0; i < strlen(str)-1; i++) {
        if (str[i]=='\n') height += font->yAdvance;
    }
    return height;
}
