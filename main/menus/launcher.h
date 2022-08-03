#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void menu_launcher(xQueueHandle button_queue);
