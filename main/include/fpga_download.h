#pragma once

#include <freertos/FreeRTOS.h>

#include "ice40.h"

bool fpga_download(xQueueHandle button_queue, ICE40* ice40);
bool fpga_host(xQueueHandle button_queue, ICE40* ice40, bool enable_uart, const char* prefix);
