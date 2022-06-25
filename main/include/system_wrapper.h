#pragma once

#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "esp_vfs.h"
#include "esp_vfs_fat.h"

void     restart();
bool     wait_for_button(xQueueHandle buttonQueue);
uint8_t* load_file_to_ram(FILE* fd);
size_t   get_file_size(FILE* fd);
