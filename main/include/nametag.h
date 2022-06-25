
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "ili9341.h"
#include "pax_gfx.h"
#include "rp2040.h"

void edit_nickname(xQueueHandle button_queue, pax_buf_t* pax_buffer, ILI9341* ili9341);
void show_nametag(xQueueHandle button_queue, pax_buf_t* pax_buffer, ILI9341* ili9341);
