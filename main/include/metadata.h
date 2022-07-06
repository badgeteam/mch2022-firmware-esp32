#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdbool.h>

#include "ili9341.h"
#include "menu.h"
#include "pax_gfx.h"
#include "appfs.h"

typedef struct {
    char* path;
    char* type;
    char* slug;
    char* title;
    char* description;
    char* category;
    char* author;
    char* license;
    int   version;
    pax_buf_t* icon;
    appfs_handle_t appfs_fd;
} launcher_app_t;

void free_launcher_app(launcher_app_t* app);

void parse_metadata(const char* path, char** name, char** description, char** category, char** author, int* version, char** license);

void populate_menu_entry_from_path(menu_t* menu, const char* path, const char* arg_type, const char* arg_name, void* default_icon_data, size_t default_icon_size);

bool populate_menu_from_path(menu_t* menu, const char* path, const char* arg_type, void* default_icon_data, size_t default_icon_size);
