#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ili9341.h"
#include "pax_gfx.h"

void wifi_connection_test(xQueueHandle button_queue);
