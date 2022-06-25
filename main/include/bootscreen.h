#pragma once

#include "ili9341.h"
#include "pax_gfx.h"

void display_boot_screen(pax_buf_t* pax_buffer, ILI9341* ili9341, const char* text);
