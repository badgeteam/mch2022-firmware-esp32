#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void edit_nickname(xQueueHandle button_queue);
void show_nametag(xQueueHandle button_queue);
