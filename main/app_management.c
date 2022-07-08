#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "appfs_wrapper.h"
#include "bootscreen.h"
#include "cJSON.h"
#include "filesystems.h"
#include "graphics_wrapper.h"
#include "http_download.h"
#include "ili9341.h"
#include "menu.h"
#include "metadata.h"
#include "pax_codecs.h"
#include "pax_gfx.h"
#include "rp2040.h"
#include "system_wrapper.h"
#include "wifi_connect.h"

static const char* TAG = "App management";

static const char* sdcard_path      = "/sd";
static const char* internal_path    = "/internal";
static const char* esp32_type       = "esp32";
static const char* esp32_bin_fn     = "main.bin";
static const char* metadata_json_fn = "metadata.json";

bool create_dir(const char* path) {
    struct stat st = {0};
    if (stat(path, &st) == 0) {
        return (st.st_mode & S_IFDIR) != 0;
    }
    return mkdir(path, 0777) == 0;
}

bool install_app(xQueueHandle button_queue, pax_buf_t* pax_buffer, ILI9341* ili9341, const char* type_slug, bool to_sd_card, char* data_app_info,
                 size_t size_app_info, cJSON* json_app_info) {
    cJSON* slug_obj = cJSON_GetObjectItem(json_app_info, "slug");
    cJSON* name_obj = cJSON_GetObjectItem(json_app_info, "name");
    // cJSON* author_obj      = cJSON_GetObjectItem(json_app_info, "author");
    // cJSON* license_obj     = cJSON_GetObjectItem(json_app_info, "license");
    // cJSON* description_obj = cJSON_GetObjectItem(json_app_info, "description");
    cJSON* version_obj = cJSON_GetObjectItem(json_app_info, "version");
    cJSON* files_obj   = cJSON_GetObjectItem(json_app_info, "files");

    char buffer[257];
    buffer[sizeof(buffer) - 1] = '\0';

    // Create folders
    snprintf(buffer, sizeof(buffer) - 1, "Installing %s:\nCreating folders...", name_obj->valuestring);
    render_message(pax_buffer, buffer);
    ili9341_write(ili9341, pax_buffer->buf);

    snprintf(buffer, sizeof(buffer) - 1, "%s/apps", to_sd_card ? sdcard_path : internal_path);
    printf("Creating dir: %s\r\n", buffer);
    if (!create_dir(buffer)) {
        // Failed to create app directory
        ESP_LOGI(TAG, "Failed to create %s", buffer);
        render_message(pax_buffer, "Failed create folder");
        ili9341_write(ili9341, pax_buffer->buf);
        if (button_queue != NULL) wait_for_button(button_queue);
        return false;
    }

    snprintf(buffer, sizeof(buffer) - 1, "%s/apps/%s", to_sd_card ? sdcard_path : internal_path, type_slug);
    printf("Creating dir: %s\r\n", buffer);
    if (!create_dir(buffer)) {
        // failed to create app type directory
        ESP_LOGI(TAG, "Failed to create %s", buffer);
        render_message(pax_buffer, "Failed create folder");
        ili9341_write(ili9341, pax_buffer->buf);
        if (button_queue != NULL) wait_for_button(button_queue);
        return false;
    }

    snprintf(buffer, sizeof(buffer) - 1, "%s/apps/%s/%s", to_sd_card ? sdcard_path : internal_path, type_slug, slug_obj->valuestring);
    printf("Creating dir: %s\r\n", buffer);
    if (!create_dir(buffer)) {
        // failed to create app directory
        ESP_LOGI(TAG, "Failed to create %s", buffer);
        render_message(pax_buffer, "Failed create folder");
        ili9341_write(ili9341, pax_buffer->buf);
        if (button_queue != NULL) wait_for_button(button_queue);
        return false;
    }

    // Download files
    cJSON* file_obj;
    cJSON_ArrayForEach(file_obj, files_obj) {
        cJSON* name_obj = cJSON_GetObjectItem(file_obj, "name");
        cJSON* url_obj  = cJSON_GetObjectItem(file_obj, "url");
        // cJSON* size_obj = cJSON_GetObjectItem(file_obj, "size");
        if ((strcmp(type_slug, esp32_type) == 0) && (strcmp(name_obj->valuestring, esp32_bin_fn) == 0)) {
            snprintf(buffer, sizeof(buffer) - 1, "Installing %s:\nDownloading '%s' to AppFS", name_obj->valuestring, name_obj->valuestring);
            render_message(pax_buffer, buffer);
            ili9341_write(ili9341, pax_buffer->buf);
            snprintf(buffer, sizeof(buffer) - 1, "%s/apps/%s/%s/%s", to_sd_card ? sdcard_path : internal_path, type_slug, slug_obj->valuestring,
                     name_obj->valuestring);
            uint8_t* esp32_binary_data;
            size_t   esp32_binary_size;
            bool     success = download_ram(url_obj->valuestring, (uint8_t**) &esp32_binary_data, &esp32_binary_size);
            if (!success) {
                ESP_LOGI(TAG, "Failed to download %s to RAM", url_obj->valuestring);
                render_message(pax_buffer, "Failed to download file");
                ili9341_write(ili9341, pax_buffer->buf);
                if (button_queue != NULL) wait_for_button(button_queue);
                return false;
            }
            if (esp32_binary_data != NULL) {  // Ignore 0 bytes files
                esp_err_t res = appfs_store_in_memory_app(button_queue, pax_buffer, ili9341, slug_obj->valuestring, name_obj->valuestring,
                                                          version_obj->valueint, esp32_binary_size, esp32_binary_data);
                if (res != ESP_OK) {
                    free(esp32_binary_data);
                    ESP_LOGI(TAG, "Failed to store ESP32 binary");
                    render_message(pax_buffer, "Failed to install app to AppFS");
                    ili9341_write(ili9341, pax_buffer->buf);
                    if (button_queue != NULL) wait_for_button(button_queue);
                    return false;
                }
                if (to_sd_card) {
                    render_message(pax_buffer, "Storing a copy of the ESP32\nbinary to the SD card...");
                    ili9341_write(ili9341, pax_buffer->buf);
                    printf("Creating file: %s\r\n", buffer);
                    FILE* binary_fd = fopen(buffer, "w");
                    if (binary_fd == NULL) {
                        free(esp32_binary_data);
                        ESP_LOGI(TAG, "Failed to install ESP32 binary to %s", buffer);
                        render_message(pax_buffer, "Failed to install app to SD card");
                        ili9341_write(ili9341, pax_buffer->buf);
                        if (button_queue != NULL) wait_for_button(button_queue);
                        return false;
                    }
                    fwrite(esp32_binary_data, 1, esp32_binary_size, binary_fd);
                    fclose(binary_fd);
                }
                free(esp32_binary_data);
            }
        } else {
            snprintf(buffer, sizeof(buffer) - 1, "Installing %s:\nDownloading '%s'...", name_obj->valuestring, name_obj->valuestring);
            render_message(pax_buffer, buffer);
            ili9341_write(ili9341, pax_buffer->buf);
            snprintf(buffer, sizeof(buffer) - 1, "%s/apps/%s/%s/%s", to_sd_card ? sdcard_path : internal_path, type_slug, slug_obj->valuestring,
                     name_obj->valuestring);
            printf("Downloading file: %s\r\n", buffer);
            if (!download_file(url_obj->valuestring, buffer)) {
                ESP_LOGI(TAG, "Failed to download %s to %s", url_obj->valuestring, buffer);
                render_message(pax_buffer, "Failed to download file");
                ili9341_write(ili9341, pax_buffer->buf);
                if (button_queue != NULL) wait_for_button(button_queue);
                return false;
            }
        }
    }

    // Install metadata.json
    snprintf(buffer, sizeof(buffer) - 1, "%s/apps/%s/%s/%s", to_sd_card ? sdcard_path : internal_path, type_slug, slug_obj->valuestring, metadata_json_fn);
    FILE* metadata_fd = fopen(buffer, "w");
    if (metadata_fd == NULL) {
        ESP_LOGI(TAG, "Failed to install metadata to %s", buffer);
        render_message(pax_buffer, "Failed to install metadata");
        ili9341_write(ili9341, pax_buffer->buf);
        if (button_queue != NULL) wait_for_button(button_queue);
        return false;
    }
    fwrite(data_app_info, 1, size_app_info, metadata_fd);
    fclose(metadata_fd);

    ESP_LOGI(TAG, "App installed!");
    render_message(pax_buffer, "App has been installed!");
    ili9341_write(ili9341, pax_buffer->buf);
    if (button_queue != NULL) wait_for_button(button_queue);
    return true;
}
