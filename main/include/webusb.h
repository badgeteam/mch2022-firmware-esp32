#pragma once

#include <freertos/FreeRTOS.h>

#include "ice40.h"
#include "ili9341.h"
#include "pax_gfx.h"

void webusb_main(xQueueHandle button_queue, pax_buf_t* pax_buffer, ILI9341* ili9341);
void webusb_new_main(xQueueHandle button_queue, pax_buf_t* pax_buffer, ILI9341* ili9341);

void webusb_enable_uart();
void webusb_disable_uart();
