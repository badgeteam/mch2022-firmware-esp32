#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void menu_dev(xQueueHandle button_queue);
