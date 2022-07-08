#pragma once

#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdbool.h>

#include "appfs.h"
#include "ili9341.h"
#include "menu.h"
#include "pax_gfx.h"

typedef struct {
    char*          path;
    char*          type;
    char*          slug;
    char*          title;
    char*          description;
    char*          category;
    char*          author;
    char*          license;
    int            version;
    pax_buf_t*     icon;
    appfs_handle_t appfs_fd;
} launcher_app_t;

typedef void (*path_callback_t)(const char*, const char*, void*);

void free_launcher_app(launcher_app_t* app);

void parse_metadata(const char* path, char** device, char** type, char** category, char** slug, char** name, char** description, char** author, int* version, char** license);

void populate_menu_entry_from_path(menu_t* menu, const char* path, const char* arg_type, const char* arg_name, void* default_icon_data,
                                   size_t default_icon_size);

bool populate_menu_from_path(menu_t* menu, const char* path, const char* arg_type, void* default_icon_data, size_t default_icon_size);

bool for_entity_in_path(const char* path, bool directories, path_callback_t callback, void* user);
