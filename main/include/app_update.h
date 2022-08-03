#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void update_apps(xQueueHandle button_queue);
