#pragma once

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "appfs.h"

esp_err_t      appfs_init(void);
appfs_handle_t appfs_detect_crash();
void           appfs_boot_app(int fd);
void           appfs_store_app(xQueueHandle button_queue, const char* path, const char* name, const char* title, uint16_t version);
esp_err_t      appfs_store_in_memory_app(xQueueHandle button_queue, const char* name, const char* title, uint16_t version, size_t app_size, uint8_t* app);
