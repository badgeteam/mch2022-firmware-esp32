#pragma once

#include "ice40.h"
#include "pax_gfx.h"
#include "ili9341.h"
#include <freertos/FreeRTOS.h>

void fpga_download(xQueueHandle buttonQueue, ICE40* ice40, pax_buf_t* pax_buffer, ILI9341* ili9341);
bool fpga_host(xQueueHandle buttonQueue, ICE40* ice40, pax_buf_t* pax_buffer, ILI9341* ili9341, bool enable_uart);
