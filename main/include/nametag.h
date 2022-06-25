
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "ili9341.h"
#include "pax_gfx.h"
#include "rp2040.h"

// Shows the name tag.
// Will fall into deep sleep if left alone for long enough.
void show_nametag(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341);
