#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void msc_main(xQueueHandle button_queue);
