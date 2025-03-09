#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void msc_main(QueueHandle_t button_queue);
