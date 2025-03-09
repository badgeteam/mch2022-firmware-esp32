#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void menu_launcher(QueueHandle_t button_queue);
