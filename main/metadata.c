#include "metadata.h"

#include <cJSON.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_vfs.h>
#include <esp_vfs_fat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>
#include "ili9341.h"
#include "menu.h"
#include "pax_codecs.h"
#include "pax_gfx.h"
#include "system_wrapper.h"

void parse_metadata(const char* path, char** name, char** description, char** category, char** author, int* revision) {
    FILE* fd = fopen(path, "r");
    if (fd == NULL) return;
    char* json_data = (char*) load_file_to_ram(fd);
    fclose(fd);
    if (json_data == NULL) return;
    cJSON* root = cJSON_Parse(json_data);
    if (root == NULL) {
        free(json_data);
        return;
    }
    if (name) {
        cJSON* name_obj = cJSON_GetObjectItem(root, "name");
        if (name_obj) {
            *name = strdup(name_obj->valuestring);
        }
    }
    if (description) {
        cJSON* description_obj = cJSON_GetObjectItem(root, "description");
        if (description_obj) {
            *description = strdup(description_obj->valuestring);
        }
    }
    if (category) {
        cJSON* category_obj = cJSON_GetObjectItem(root, "category");
        if (category_obj) {
            *category = strdup(category_obj->valuestring);
        }
    }
    if (author) {
        cJSON* author_obj = cJSON_GetObjectItem(root, "author");
        if (author_obj) {
            *author = strdup(author_obj->valuestring);
        }
    }
    if (revision) {
        cJSON* revision_obj = cJSON_GetObjectItem(root, "revision");
        if (revision_obj) {
            *revision = revision_obj->valueint;
        }
    }
    cJSON_Delete(root);
}

void populate_menu_entry_from_path(menu_t* menu, const char* path,
                                   const char* name, void* default_icon_data, size_t default_icon_size) {  // Path is here the folder of a specific app, for example /internal/apps/event_schedule
    char metadata_file_path[128];
    snprintf(metadata_file_path, sizeof(metadata_file_path), "%s/%s/metadata.json", path, name);
    char icon_file_path[128];
    snprintf(icon_file_path, sizeof(icon_file_path), "%s/%s/icon.png", path, name);
    char app_path[128];
    snprintf(app_path, sizeof(app_path), "%s/%s", path, name);

    char* title = NULL;
    /*char* description = NULL;
    char* category    = NULL;
    char* author      = NULL;
    int   revision    = -1;*/

    // parse_metadata(metadata_file_path, &title, &description, &category, &author, &revision);
    parse_metadata(metadata_file_path, &title, NULL, NULL, NULL, NULL);

    /*if (title != NULL) printf("Name: %s\n", title);
    if (description != NULL) printf("Description: %s\n", description);
    if (category != NULL) printf("Category: %s\n", category);
    if (author != NULL) printf("Author: %s\n", author);
    if (revision >= 0) printf("Revision: %u\n", revision);*/

    pax_buf_t* icon = NULL;

    FILE* icon_fd = fopen(icon_file_path, "rb");
    if (icon_fd != NULL) {
        size_t   icon_size = get_file_size(icon_fd);
        uint8_t* icon_data = load_file_to_ram(icon_fd);
        if (icon_data != NULL) {
            icon = malloc(sizeof(pax_buf_t));
            if (icon != NULL) {
                pax_decode_png_buf(icon, (void*) icon_data, icon_size, PAX_BUF_32_8888ARGB, 0);
            }
            free(icon_data);
        }
        fclose(icon_fd);
    }

    if ((icon == NULL) && (default_icon_data != NULL)) {
        icon = malloc(sizeof(pax_buf_t));
        if (icon != NULL) {
            pax_decode_png_buf(icon, default_icon_data, default_icon_size, PAX_BUF_32_8888ARGB, 0);
        }
    }
    
    menu_insert_item_icon(menu, (title != NULL) ? title : name, NULL, (void*) strdup(app_path), -1, icon);

    if (title) free(title);
    /*if (description) free(description);
    if (category) free(category);
    if (author) free(author);*/
}

bool populate_menu_from_path(menu_t* menu, const char* path, void* default_icon_data, size_t default_icon_size) {  // Path is here the folder containing the Python apps, for example /internal/apps
    DIR* dir = opendir(path);
    if (dir == NULL) {
        printf("Failed to populate menu, directory not found: %s\n", path);
        return false;
    }
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type == DT_REG) continue;  // Skip files, only parse directories
        populate_menu_entry_from_path(menu, path, ent->d_name, default_icon_data, default_icon_size);
    }
    closedir(dir);
    return true;
}
