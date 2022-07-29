#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>

void fpga_test(xQueueHandle button_queue);
bool run_fpga_tests(xQueueHandle button_queue);
