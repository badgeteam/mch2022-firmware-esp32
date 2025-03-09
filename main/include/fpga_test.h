#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void fpga_test(QueueHandle_t button_queue);
bool run_fpga_tests(QueueHandle_t button_queue);
