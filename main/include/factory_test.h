#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#include "ili9341.h"
#include "pax_gfx.h"

void factory_test(pax_buf_t* pax_buffer, ILI9341* ili9341);
