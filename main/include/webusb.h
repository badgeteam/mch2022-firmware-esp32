#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void webusb_main(xQueueHandle button_queue);
void webusb_new_main(xQueueHandle button_queue);
