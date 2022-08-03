#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void test_buttons(xQueueHandle button_queue);
