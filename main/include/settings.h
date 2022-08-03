#pragma once

#include <esp_err.h>
#include <stdint.h>

esp_err_t nvs_init();
esp_err_t nvs_get_str_fixed(const char* nvs_namespace, const char* key, char* target, size_t target_size, size_t* size);
uint8_t   nvs_get_u8_default(const char* nvs_namespace, const char* key, uint8_t default_value);
esp_err_t nvs_set_u8_fixed(const char* nvs_namespace, const char* key, uint8_t value);
