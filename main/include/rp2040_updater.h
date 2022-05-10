#pragma once

#include <stdint.h>
#include <sdkconfig.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "pax_gfx.h"
#include "ili9341.h"


void rp2040_updater(RP2040* rp2040, pax_buf_t* pax_buffer, ILI9341* ili9341, uint8_t* framebuffer);
