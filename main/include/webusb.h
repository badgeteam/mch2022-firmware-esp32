#pragma once

#include "ice40.h"
#include "pax_gfx.h"
#include "ili9341.h"
#include <freertos/FreeRTOS.h>

void webusb_main(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341);

void webusb_enable_uart();
void webusb_disable_uart();