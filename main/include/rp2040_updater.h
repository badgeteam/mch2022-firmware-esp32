#pragma once

#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdint.h>

#include "ili9341.h"
#include "pax_gfx.h"

void rp2040_update_start(RP2040* rp2040, pax_buf_t* pax_buffer, ILI9341* ili9341);
void rp2040_updater(RP2040* rp2040, pax_buf_t* pax_buffer, ILI9341* ili9341);
