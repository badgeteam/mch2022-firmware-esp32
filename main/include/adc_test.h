#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>

#include "ili9341.h"
#include "pax_gfx.h"

void test_adc(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341);
