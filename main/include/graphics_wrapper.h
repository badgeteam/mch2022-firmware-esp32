#pragma once

#include <stdint.h>
#include <sdkconfig.h>
#include <esp_system.h>

#include "pax_gfx.h"
#include "ili9341.h"
#include "menu.h"


esp_err_t graphics_task(pax_buf_t* pax_buffer, ILI9341* ili9341, uint8_t* framebuffer, menu_t* menu, char* message);
