#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void terminal_log(char* fmt, ...);
void terminal_log_wrapped(char* buffer);
void terminal_start();
