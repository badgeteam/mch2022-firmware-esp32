#pragma once

#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdint.h>

#include "ili9341.h"
#include "menu.h"
#include "pax_gfx.h"

void render_header(pax_buf_t* pax_buffer, float position_x, float position_y, float width, float height, float text_height, pax_col_t text_color,
                   pax_col_t bg_color, pax_buf_t* icon, const char* label);
void render_message(pax_buf_t* pax_buffer, char* message);
bool keyboard(xQueueHandle buttonQueue, pax_buf_t* aBuffer, ILI9341* ili9341, float aPosX, float aPosY, float aWidth, float aHeight, const char* aTitle,
              const char* aHint, char* aOutput, size_t aOutputSize);
