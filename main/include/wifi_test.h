#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

void wifi_connection_test(QueueHandle_t button_queue);
