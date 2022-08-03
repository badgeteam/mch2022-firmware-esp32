#pragma once

#include "esp_vfs.h"

void     restart();
bool     wait_for_button();
uint8_t* load_file_to_ram(FILE* fd);
size_t   get_file_size(FILE* fd);
bool     remove_recursive(const char* path);
