#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void webusb_main(QueueHandle_t button_queue);
void webusb_new_main(QueueHandle_t button_queue);
