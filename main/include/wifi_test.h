#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "ili9341.h"
#include "pax_gfx.h"

void wifi_connection_test(xQueueHandle button_queue, pax_buf_t* pax_buffer, ILI9341* ili9341);
