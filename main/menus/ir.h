#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdbool.h>

void menu_ir(QueueHandle_t button_queue, bool alternative);
