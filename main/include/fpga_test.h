#pragma once

#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "hardware.h"
#include "rp2040.h"
#include "ili9341.h"
#include "ice40.h"
#include "pax_gfx.h"

void fpga_test(xQueueHandle buttonQueue, ICE40* ice40, pax_buf_t* pax_buffer, ILI9341* ili9341);
