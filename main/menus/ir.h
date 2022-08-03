#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdbool.h>

void menu_ir(xQueueHandle button_queue, bool alternative);
