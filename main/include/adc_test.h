#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void test_adc(QueueHandle_t button_queue);
