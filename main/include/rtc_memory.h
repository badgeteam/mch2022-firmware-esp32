#pragma once

#include <esp_system.h>

esp_err_t rtc_memory_int_write(int pos, int val);
esp_err_t rtc_memory_int_read(int pos, int* val);
esp_err_t rtc_memory_string_write(const char* str);
esp_err_t rtc_memory_string_read(const char** str);
esp_err_t rtc_memory_clear();
