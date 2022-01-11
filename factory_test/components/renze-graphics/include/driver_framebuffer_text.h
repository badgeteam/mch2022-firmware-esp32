#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Fonts */

#ifdef __cplusplus
extern "C" {
#endif

extern const GFXfont ocra_22pt7b;

/* Functions */

uint16_t driver_framebuffer_print(Window* window, const char* str, int16_t x0, int16_t y0, uint8_t xScale, uint8_t yScale, uint32_t color, const GFXfont *font);
uint16_t driver_framebuffer_print_len(Window* window, const char* str, int16_t len, int16_t x0, int16_t y0, uint8_t xScale, uint8_t yScale, uint32_t color, const GFXfont *font);
uint16_t driver_framebuffer_get_string_width(const char* str, const GFXfont *font);
uint16_t driver_framebuffer_get_string_height(const char* str, const GFXfont *font);

#ifdef __cplusplus
}
#endif
