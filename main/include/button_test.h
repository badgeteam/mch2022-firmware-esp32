#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void test_buttons(QueueHandle_t button_queue);
