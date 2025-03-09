#pragma once

#include <freertos/FreeRTOS.h>

#include "ice40.h"

void fpga_download(QueueHandle_t button_queue, ICE40* ice40);
bool fpga_host(QueueHandle_t button_queue, ICE40* ice40, bool enable_uart, const char* prefix);
