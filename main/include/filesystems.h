#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t mount_internal_filesystem();
esp_err_t format_internal_filesystem();
esp_err_t mount_sdcard_filesystem();
bool      get_sdcard_mounted();
void      get_internal_filesystem_size_and_available(uint64_t* fs_size, uint64_t* fs_free);
void      get_sdcard_filesystem_size_and_available(uint64_t* fs_size, uint64_t* fs_free);
