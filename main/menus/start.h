#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void menu_start(xQueueHandle button_queue, const char* version);
