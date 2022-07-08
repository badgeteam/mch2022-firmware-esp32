#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "ili9341.h"
#include "pax_gfx.h"

void update_apps(xQueueHandle button_queue, pax_buf_t* pax_buffer, ILI9341* ili9341);
