#pragma once

#include <stdint.h>
#include <sdkconfig.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "pax_gfx.h"
#include "ili9341.h"
#include "menu.h"


esp_err_t graphics_task(pax_buf_t* pax_buffer, ILI9341* ili9341, uint8_t* framebuffer, menu_t* menu, char* message);
bool keyboard(xQueueHandle buttonQueue, pax_buf_t* aBuffer, ILI9341* ili9341, uint8_t* framebuffer, float aPosX, float aPosY, float aWidth, float aHeight, const char* aTitle, const char* aHint, char* aOutput, size_t aOutputSize);
