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

static const char* TAG = "Metadata";

void parse_metadata(const char* path, char** name, char** description, char** category, char** author, int* version, char** license) {
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
    if (version) {
        cJSON* version_obj = cJSON_GetObjectItem(root, "version");
        if (version_obj) {
            *version = version_obj->valueint;
        }
    }
    if (license) {
        cJSON* license_obj = cJSON_GetObjectItem(root, "license");
        if (license_obj) {
            *license = strdup(license_obj->valuestring);
        }
    }
    cJSON_Delete(root);
}

void free_launcher_app(launcher_app_t* app) {
    if (app->path) free(app->path);
    if (app->type) free(app->type);
    if (app->slug) free(app->slug);
    if (app->title) free(app->title);
    if (app->description) free(app->description);
    if (app->category) free(app->category);
    if (app->author) free(app->author);
    if (app->license) free(app->license);
    if (app->icon) pax_buf_destroy(app->icon);
    free(app);
}

static appfs_handle_t find_appfs_handle_for_slug(const char* search_slug) {
    appfs_handle_t appfs_fd = appfsNextEntry(APPFS_INVALID_FD);
    while (appfs_fd != APPFS_INVALID_FD) {
        const char* slug = NULL;
        appfsEntryInfoExt(appfs_fd, &slug, NULL, NULL, NULL);
        if ((strlen(search_slug) == strlen(slug)) && (strcmp(search_slug, slug) == 0)) {
            return appfs_fd;
        }
        appfs_fd = appfsNextEntry(appfs_fd);
    }

    return APPFS_INVALID_FD;
}

void populate_menu_entry_from_path(menu_t* menu, const char* path, const char* type, const char* slug, void* default_icon_data, size_t default_icon_size) {
    char metadata_file_path[128];
    snprintf(metadata_file_path, sizeof(metadata_file_path), "%s/%s/metadata.json", path, slug);
    char icon_file_path[128];
    snprintf(icon_file_path, sizeof(icon_file_path), "%s/%s/icon.png", path, slug);
    char app_path[128];
    snprintf(app_path, sizeof(app_path), "%s/%s", path, slug);

    launcher_app_t* app = malloc(sizeof(launcher_app_t));
    if (app == NULL) {
        ESP_LOGE(TAG, "Malloc for app entry failed");
        return;
    }
    memset(app, 0, sizeof(launcher_app_t));
    app->appfs_fd = (strcmp(type, "esp32") == 0) ? find_appfs_handle_for_slug(slug) : APPFS_INVALID_FD;
    app->path     = strdup(app_path);
    app->type     = strdup(type);
    app->slug     = strdup(slug);
    parse_metadata(metadata_file_path, &app->title, &app->description, &app->category, &app->author, &app->version, &app->license);
    app->icon = NULL;

    FILE* icon_fd = fopen(icon_file_path, "rb");
    if (icon_fd != NULL) {
        size_t   icon_size = get_file_size(icon_fd);
        uint8_t* icon_data = load_file_to_ram(icon_fd);
        if (icon_data != NULL) {
            app->icon = malloc(sizeof(pax_buf_t));
            if (app->icon != NULL) {
                pax_decode_png_buf(app->icon, (void*) icon_data, icon_size, PAX_BUF_32_8888ARGB, 0);
            }
            free(icon_data);
        }
        fclose(icon_fd);
    }

    if ((app->icon == NULL) && (default_icon_data != NULL)) {
        app->icon = malloc(sizeof(pax_buf_t));
        if (app->icon != NULL) {
            pax_decode_png_buf(app->icon, default_icon_data, default_icon_size, PAX_BUF_32_8888ARGB, 0);
        }
    }

    menu_insert_item_icon(menu, (app->title != NULL) ? app->title : app->slug, NULL, (void*) app, -1, app->icon);
}

bool populate_menu_from_path(menu_t* menu, const char* path, const char* arg_type, void* default_icon_data,
                             size_t default_icon_size) {  // Path is here the folder containing the Python apps, for example /internal/apps
    char path_with_type[256];
    path_with_type[sizeof(path_with_type) - 1] = '\0';
    snprintf(path_with_type, sizeof(path_with_type), "%s/%s", path, arg_type);
    DIR* dir = opendir(path_with_type);
    if (dir == NULL) {
        printf("Failed to populate menu, directory not found: %s\n", path_with_type);
        return false;
    }
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type == DT_REG) continue;  // Skip files, only parse directories
        populate_menu_entry_from_path(menu, path_with_type, arg_type, ent->d_name, default_icon_data, default_icon_size);
    }
    closedir(dir);
    return true;
}
