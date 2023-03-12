#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void terminal_printf(char* fmt, ...);
void terminal_log(char* buffer);
void terminal_start();
