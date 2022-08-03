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

#include "menu.h"
#include "pax_codecs.h"
#include "pax_gfx.h"
#include "system_wrapper.h"

static const char* TAG = "Metadata";

void parse_metadata(const char* path, char** device, char** type, char** category, char** slug, char** name, char** description, char** author, int* version,
                    char** license) {
    FILE* fd = fopen(path, "r");
    if (fd == NULL) {
        ESP_LOGW(TAG, "Failed to open metadata file %s", path);
        return;
    }
    char* json_data = (char*) load_file_to_ram(fd);
    fclose(fd);
    if (json_data == NULL) return;
    cJSON* root = cJSON_Parse(json_data);
    if (root == NULL) {
        free(json_data);
        return;
    }
    if (device) {
        cJSON* device_obj = cJSON_GetObjectItem(root, "device");
        if (device_obj && (device_obj->valuestring != NULL)) {
            *device = strdup(device_obj->valuestring);
        }
    }
    if (type) {
        cJSON* type_obj = cJSON_GetObjectItem(root, "type");
        if (type_obj && (type_obj->valuestring != NULL)) {
            *type = strdup(type_obj->valuestring);
        }
    }
    if (category) {
        cJSON* category_obj = cJSON_GetObjectItem(root, "category");
        if (category_obj && (category_obj->valuestring != NULL)) {
            *category = strdup(category_obj->valuestring);
        }
    }
    if (slug) {
        cJSON* slug_obj = cJSON_GetObjectItem(root, "slug");
        if (slug_obj && (slug_obj->valuestring != NULL)) {
            *slug = strdup(slug_obj->valuestring);
        }
    }
    if (name) {
        cJSON* name_obj = cJSON_GetObjectItem(root, "name");
        if (name_obj && (name_obj->valuestring != NULL)) {
            *name = strdup(name_obj->valuestring);
        }
    }
    if (description) {
        cJSON* description_obj = cJSON_GetObjectItem(root, "description");
        if (description_obj && (description_obj->valuestring != NULL)) {
            *description = strdup(description_obj->valuestring);
        }
    }
    if (author) {
        cJSON* author_obj = cJSON_GetObjectItem(root, "author");
        if (author_obj && (author_obj->valuestring != NULL)) {
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
        if (license_obj && (license_obj->valuestring != NULL)) {
            *license = strdup(license_obj->valuestring);
        }
    }
    cJSON_Delete(root);
    free(json_data);
}

void free_launcher_app(launcher_app_t* app) {
    if (app->path) {
        free(app->path);
        app->path = NULL;
    }
    if (app->type) {
        free(app->type);
        app->type = NULL;
    }
    if (app->slug) {
        free(app->slug);
        app->slug = NULL;
    }
    if (app->title) {
        free(app->title);
        app->title = NULL;
    }
    if (app->description) {
        free(app->description);
        app->description = NULL;
    }
    if (app->category) {
        free(app->category);
        app->category = NULL;
    }
    if (app->author) {
        free(app->author);
        app->author = NULL;
    }
    if (app->license) {
        free(app->license);
        app->license = NULL;
    }
    if (app->icon) {
        pax_buf_destroy(app->icon);
        free(app->icon);
        app->icon = NULL;
    }
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
    parse_metadata(metadata_file_path, NULL, NULL, &app->category, NULL, &app->title, &app->description, &app->author, &app->version, &app->license);
    app->icon = NULL;

    FILE* icon_fd = fopen(icon_file_path, "rb");
    if (icon_fd != NULL) {
        size_t   icon_size = get_file_size(icon_fd);
        uint8_t* icon_data = load_file_to_ram(icon_fd);
        if (icon_data != NULL) {
            app->icon = malloc(sizeof(pax_buf_t));
            if (app->icon != NULL) {
                if (!pax_decode_png_buf(app->icon, (void*) icon_data, icon_size, PAX_BUF_32_8888ARGB, 0)) {
                    free(app->icon);
                    app->icon = NULL;
                }
            }
            free(icon_data);
        }
        fclose(icon_fd);
    }

    if ((app->icon == NULL) && (default_icon_data != NULL)) {
        app->icon = malloc(sizeof(pax_buf_t));
        if (app->icon != NULL) {
            if (!pax_decode_png_buf(app->icon, default_icon_data, default_icon_size, PAX_BUF_32_8888ARGB, 0)) {
                free(app->icon);
                app->icon = NULL;
            }
        }
    }

    menu_insert_item_icon(menu, (app->title != NULL) ? app->title : app->slug, NULL, (void*) app, -1, app->icon);
}

bool populate_menu_from_path(menu_t* menu, const char* path, const char* arg_type, void* default_icon_data,
                             size_t default_icon_size) {  // Path is here the folder containing the apps, for example /internal/apps
    char path_with_type[256];
    path_with_type[sizeof(path_with_type) - 1] = '\0';
    snprintf(path_with_type, sizeof(path_with_type), "%s/%s", path, arg_type);
    DIR* dir = opendir(path_with_type);
    if (dir == NULL) {
        ESP_LOGW(TAG, "Directory not found: %s", path_with_type);
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

bool for_entity_in_path(const char* path, bool directories, path_callback_t callback, void* user) {
    DIR* dir = opendir(path);
    if (dir == NULL) {
        ESP_LOGW(TAG, "Directory not found: %s", path);
        return false;
    }
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if ((directories) && (ent->d_type == DT_REG)) continue;   // Skip files, only parse directories
        if ((!directories) && (ent->d_type != DT_REG)) continue;  // Skip directories, only parse files
        callback(path, ent->d_name, user);
    }
    closedir(dir);
    return true;
}
