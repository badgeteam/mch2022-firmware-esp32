#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void update_apps(QueueHandle_t button_queue);
