#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void edit_nickname(QueueHandle_t button_queue);
void show_nametag(QueueHandle_t button_queue);
