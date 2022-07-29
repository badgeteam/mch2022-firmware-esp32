#pragma once

#include <freertos/FreeRTOS.h>

#include "ice40.h"

void fpga_download(xQueueHandle buttonQueue, ICE40* ice40);
bool fpga_host(xQueueHandle buttonQueue, ICE40* ice40, bool enable_uart, const char* prefix);
