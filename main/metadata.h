#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdbool.h>

#include "ili9341.h"
#include "menu.h"
#include "pax_gfx.h"

void parse_metadata(const char* path, char** name, char** description, char** category, char** author, int* revision);
void populate_menu_entry_from_path(menu_t* menu, const char* path, const char* name, void* default_icon_data, size_t default_icon_size);
bool populate_menu_from_path(menu_t* menu, const char* path, void* default_icon_data, size_t default_icon_size);
