#pragma once

#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdint.h>

#include "pax_gfx.h"

void render_outline(float position_x, float position_y, float width, float height, pax_col_t border_color, pax_col_t background_color);
void render_message(char* message);
bool keyboard(xQueueHandle button_queue, float aPosX, float aPosY, float aWidth, float aHeight, const char* aTitle, const char* aHint, char* aOutput, size_t aOutputSize);
