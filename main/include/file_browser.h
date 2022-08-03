#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void list_files_in_folder(const char* path);
void file_browser(xQueueHandle button_queue, const char* initial_path);
