#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "rp2040.h"

void rp2040_update_start(RP2040* rp2040);
void rp2040_updater(RP2040* rp2040);
