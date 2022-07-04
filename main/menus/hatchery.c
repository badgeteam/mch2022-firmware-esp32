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
#include "ili9341.h"
#include "menu.h"
#include "pax_codecs.h"
#include "pax_gfx.h"
#include "rp2040.h"
#include "bootscreen.h"
#include "graphics_wrapper.h"
#include "system_wrapper.h"
#include "http_download.h"
#include "wifi_connect.h"
#include "cJSON.h"
#include "filesystems.h"


static const char *TAG = "Hatchery";

extern const uint8_t hatchery_png_start[] asm("_binary_hatchery_png_start");
extern const uint8_t hatchery_png_end[] asm("_binary_hatchery_png_end");

const char* sdcard_path = "/sd";
const char* internal_path = "/internal";
const char* esp32_type = "esp32";
const char* esp32_bin_fn = "main.bin";
const char* metadata_json_fn = "metadata.json";

static bool create_dir(const char *path) {
    struct stat st = {0};
    if (stat(path, &st) == 0) {
        return (st.st_mode & S_IFDIR) != 0;
    }
    return mkdir(path, 0777) == 0;
}

static menu_t* hatchery_menu_create(const char* title) {
    menu_t *menu = menu_alloc(title, 34, 18);
    menu->fgColor           = 0xFF000000;
    menu->bgColor           = 0xFFFFFFFF;
    menu->bgTextColor       = 0xFFFFFFFF;
    menu->selectedItemColor = 0xFFfa448c;
    menu->borderColor       = 0xFF491d88;
    menu->titleColor        = 0xFFfa448c;
    menu->titleBgColor      = 0xFF491d88;
    menu->scrollbarBgColor  = 0xFFCCCCCC;
    menu->scrollbarFgColor  = 0xFF555555;
    pax_buf_t* icon_hatchery = malloc(sizeof(pax_buf_t));
    if (icon_hatchery) {
        pax_decode_png_buf(icon_hatchery, (void *) hatchery_png_start, hatchery_png_end - hatchery_png_start, PAX_BUF_32_8888ARGB, 0);
        menu_set_icon(menu, icon_hatchery);
    }
    return menu;
}

static void hatchery_menu_destroy(menu_t* menu) {
    pax_buf_destroy(menu->icon);
    free(menu->icon);
    menu_free(menu);
}

int wait_for_button_press(xQueueHandle button_queue, TickType_t timeout) {
    int button = -1;
    while (true) {
        rp2040_input_message_t message;
        xQueueReceive(button_queue, &message, portMAX_DELAY);
        button = message.input;
        if (message.state) break;
    }
    return button;
}

static void* hatchery_menu_show(xQueueHandle button_queue, pax_buf_t *pax_buffer, ILI9341 *ili9341, menu_t* menu, const char* prompt, bool* back_btn, bool* select_btn, bool* menu_btn, bool* home_btn) {
    bool quit = false;
    bool render = true;
    void* return_value = NULL;
    while (!quit) {
        if (render) {
            pax_background(pax_buffer, 0xFFFFFF);
            menu_render(pax_buffer, menu, 0, 0, 320, 220);
            pax_draw_text(pax_buffer, 0xFF491d88, pax_font_saira_regular, 18, 5, pax_buffer->height - 18, prompt);
            ili9341_write(ili9341, pax_buffer->buf);
            render = false;
        }

        int button = wait_for_button_press(button_queue, portMAX_DELAY);
        switch (button) {
            case RP2040_INPUT_JOYSTICK_DOWN:
                menu_navigate_next(menu);
                render = true;
                break;
            case RP2040_INPUT_JOYSTICK_UP:
                menu_navigate_previous(menu);
                render = true;
                break;
            case RP2040_INPUT_BUTTON_ACCEPT:
            case RP2040_INPUT_JOYSTICK_PRESS:
            case RP2040_INPUT_BUTTON_START:
                return_value = menu_get_callback_args(menu, menu_get_position(menu));
                quit = true;
                break;
            case RP2040_INPUT_BUTTON_BACK:
                quit = true;
                if (back_btn) *back_btn = true;
                break;
            case RP2040_INPUT_BUTTON_SELECT:
                quit = true;
                if (select_btn) *select_btn = true;
                break;
            case RP2040_INPUT_BUTTON_MENU:
                quit = true;
                if (menu_btn) *menu_btn = true;
                break;
            case RP2040_INPUT_BUTTON_HOME:
                quit = true;
                if (home_btn) *home_btn = true;
                break;
            default:
                break;
        }
    }
    
    return return_value;
}

static char* data_types = NULL;
static size_t size_types = 0;
static cJSON* json_types = NULL;

static char* data_categories = NULL;
static size_t size_categories = 0;
static cJSON* json_categories = NULL;

static char* data_apps = NULL;
static size_t size_apps = 0;
static cJSON* json_apps = NULL;

static char* data_app_info = NULL;
static size_t size_app_info = 0;
static cJSON* json_app_info = NULL;

static bool connect_to_wifi(xQueueHandle button_queue, pax_buf_t *pax_buffer, ILI9341 *ili9341) {
    if (!wifi_connect_to_stored()) {
        wifi_disconnect_and_disable();
        render_message(pax_buffer, "Unable to connect to\nthe WiFi network");
        ili9341_write(ili9341, pax_buffer->buf);
        wait_for_button(button_queue);
        return false;
    }
    return true;
}

static void hatchery_free_types() {
    if (json_types != NULL) {
        cJSON_Delete(json_types);
        json_types = NULL;
    }

    if (data_types != NULL) {
        free(data_types);
        data_types = NULL;
        size_types = 0;
    }
}

static void hatchery_free_categories() {
    if (json_categories != NULL) {
        cJSON_Delete(json_categories);
        json_categories = NULL;
    }

    if (data_categories != NULL) {
        free(data_categories);
        data_categories = NULL;
        size_categories = 0;
    }
}

static void hatchery_free_apps() {
    if (json_apps != NULL) {
        cJSON_Delete(json_apps);
        json_apps = NULL;
    }

    if (data_apps != NULL) {
        free(data_apps);
        data_apps = NULL;
        size_apps = 0;
    }
}

static void hatchery_free_app_info() {
    if (json_app_info != NULL) {
        cJSON_Delete(json_app_info);
        json_app_info = NULL;
    }

    if (data_app_info != NULL) {
        free(data_app_info);
        data_app_info = NULL;
        size_app_info = 0;
    }
}

static void hatchery_free() {
    hatchery_free_types();
    hatchery_free_categories();
    hatchery_free_apps();
    hatchery_free_app_info();
}

static void show_communication_error(xQueueHandle button_queue, pax_buf_t *pax_buffer, ILI9341 *ili9341) {
    render_message(pax_buffer, "Unable to communicate with\nthe Hatchery server");
    ili9341_write(ili9341, pax_buffer->buf);
    wait_for_button(button_queue);
}

static bool load_types() {
    if (data_types == NULL) {
        bool success = download_ram("https://mch2022.badge.team/v2/mch2022/types", (uint8_t**) &data_types, &size_types);
        if (!success) return false;
    }
    if (data_types == NULL) return false;
    json_types = cJSON_ParseWithLength(data_types, size_types);
    if (json_types == NULL) return false;
    return true;
}

static bool load_categories(const char* type_slug) {
    char url[128];
    snprintf(url, sizeof(url) - 1, "https://mch2022.badge.team/v2/mch2022/%s/categories", type_slug);
    bool success = download_ram(url, (uint8_t**) &data_categories, &size_categories);
    if (!success) return false;
    if (data_categories == NULL) return false;
    json_categories = cJSON_ParseWithLength(data_categories, size_categories);
    if (json_categories == NULL) return false;
    return true;
}

static bool load_apps(const char* type_slug, const char* category_slug) {
    char url[128];
    snprintf(url, sizeof(url) - 1, "https://mch2022.badge.team/v2/mch2022/%s/%s", type_slug, category_slug);
    bool success = download_ram(url, (uint8_t**) &data_apps, &size_apps);
    if (!success) return false;
    if (data_apps == NULL) return false;
    json_apps = cJSON_ParseWithLength(data_apps, size_apps);
    if (json_apps == NULL) return false;
    return true;
}

static bool load_app_info(const char* type_slug, const char* category_slug, const char* app_slug) {
    char url[128];
    snprintf(url, sizeof(url) - 1, "https://mch2022.badge.team/v2/mch2022/%s/%s/%s", type_slug, category_slug, app_slug);
    bool success = download_ram(url, (uint8_t**) &data_app_info, &size_app_info);
    if (!success) return false;
    if (data_app_info == NULL) return false;
    json_app_info = cJSON_ParseWithLength(data_app_info, size_app_info);
    if (json_app_info == NULL) return false;
    return true;
}

bool menu_hatchery_install_app_execute(xQueueHandle button_queue, pax_buf_t *pax_buffer, ILI9341 *ili9341, const char* type_slug, const char* category_slug, const char* app_slug, bool to_sd_card) {
    cJSON* slug_obj = cJSON_GetObjectItem(json_app_info, "slug");
    cJSON* app_name_obj = cJSON_GetObjectItem(json_app_info, "name");
    cJSON* author_obj = cJSON_GetObjectItem(json_app_info, "author");
    cJSON* license_obj = cJSON_GetObjectItem(json_app_info, "license");
    cJSON* description_obj = cJSON_GetObjectItem(json_app_info, "description");
    cJSON* version_obj = cJSON_GetObjectItem(json_app_info, "version");
    cJSON* files_obj = cJSON_GetObjectItem(json_app_info, "files");

    char buffer[257];
    buffer[sizeof(buffer) - 1] = '\0';

    // Create folders
    snprintf(buffer, sizeof(buffer) - 1, "Installing %s:\nCreating folders...", app_name_obj->valuestring);
    render_message(pax_buffer, buffer);
    ili9341_write(ili9341, pax_buffer->buf);

    snprintf(buffer, sizeof(buffer) - 1, "%s/apps", to_sd_card ? sdcard_path : internal_path);
    printf("Creating dir: %s\r\n", buffer);
    if (!create_dir(buffer)) {
        // Failed to create app directory
        ESP_LOGI(TAG, "Failed to create %s", buffer);
        render_message(pax_buffer, "Failed create folder");
        ili9341_write(ili9341, pax_buffer->buf);
        wait_for_button(button_queue);
        return false;
    }

    printf("Creating dir: %s\r\n", buffer);
    snprintf(buffer, sizeof(buffer) - 1, "%s/apps/%s", to_sd_card ? sdcard_path : internal_path, type_slug);
    if (!create_dir(buffer)) {
        // failed to create app type directory
        ESP_LOGI(TAG, "Failed to create %s", buffer);
        render_message(pax_buffer, "Failed create folder");
        ili9341_write(ili9341, pax_buffer->buf);
        wait_for_button(button_queue);
        return false;
    }

    printf("Creating dir: %s\r\n", buffer);
    snprintf(buffer, sizeof(buffer) - 1, "%s/apps/%s/%s", to_sd_card ? sdcard_path : internal_path, type_slug, app_slug);
    if (!create_dir(buffer)) {
        // failed to create app directory
        ESP_LOGI(TAG, "Failed to create %s", buffer);
        render_message(pax_buffer, "Failed create folder");
        ili9341_write(ili9341, pax_buffer->buf);
        wait_for_button(button_queue);
        return false;
    }

    // Download files
    cJSON* file_obj;
    cJSON_ArrayForEach(file_obj, files_obj) {
        cJSON* name_obj = cJSON_GetObjectItem(file_obj, "name");
        cJSON* url_obj = cJSON_GetObjectItem(file_obj, "url");
        cJSON* size_obj = cJSON_GetObjectItem(file_obj, "size");
        if ((strcmp(type_slug, esp32_type) == 0) && (strcmp(name_obj->valuestring, esp32_bin_fn))) {
            snprintf(buffer, sizeof(buffer) - 1, "Installing %s:\nDownloading '%s' to AppFS", app_name_obj->valuestring, name_obj->valuestring);
            render_message(pax_buffer, buffer);
            ili9341_write(ili9341, pax_buffer->buf);
            snprintf(buffer, sizeof(buffer) - 1, "%s/apps/%s/%s/%s", to_sd_card ? sdcard_path : internal_path, type_slug, app_slug, name_obj->valuestring);
            uint8_t* esp32_binary_data;
            size_t esp32_binary_size;
            bool success = download_ram(url_obj->valuestring, (uint8_t**) &esp32_binary_data, &esp32_binary_size);
            if (!success) {
                ESP_LOGI(TAG, "Failed to download %s to RAM", url_obj->valuestring);
                render_message(pax_buffer, "Failed to download file");
                ili9341_write(ili9341, pax_buffer->buf);
                wait_for_button(button_queue);
                return false;
            }
            esp_err_t res = appfs_store_in_memory_app(button_queue, pax_buffer, ili9341, app_slug, name_obj->valuestring, version_obj->valueint, esp32_binary_size, esp32_binary_data);
            if (res != ESP_OK) {
                free(esp32_binary_data);
                ESP_LOGI(TAG, "Failed to store ESP32 binary");
                render_message(pax_buffer, "Failed to install app to AppFS");
                ili9341_write(ili9341, pax_buffer->buf);
                wait_for_button(button_queue);
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
                    wait_for_button(button_queue);
                    return false;
                }
                fwrite(esp32_binary_data, 1, esp32_binary_size, binary_fd);
                fclose(binary_fd);
                free(esp32_binary_data);
            }
            free(esp32_binary_data);
        } else {
            snprintf(buffer, sizeof(buffer) - 1, "Installing %s:\nDownloading '%s'...", app_name_obj->valuestring, name_obj->valuestring);
            render_message(pax_buffer, buffer);
            ili9341_write(ili9341, pax_buffer->buf);
            snprintf(buffer, sizeof(buffer) - 1, "%s/apps/%s/%s/%s", to_sd_card ? sdcard_path : internal_path, type_slug, app_slug, name_obj->valuestring);
            printf("Downloading file: %s\r\n", buffer);
            if (!download_file(url_obj->valuestring, buffer)) {
                ESP_LOGI(TAG, "Failed to download %s to %s", url_obj->valuestring, buffer);
                render_message(pax_buffer, "Failed to download file");
                ili9341_write(ili9341, pax_buffer->buf);
                wait_for_button(button_queue);
                return false;
            }
        }
    }

    // Install metadata.json
    snprintf(buffer, sizeof(buffer) - 1, "%s/apps/%s/%s/%s", to_sd_card ? sdcard_path : internal_path, type_slug, app_slug, metadata_json_fn);
    FILE* metadata_fd = fopen(buffer, "w");
    if (metadata_fd == NULL) {
        ESP_LOGI(TAG, "Failed to install metadata to %s", buffer);
        render_message(pax_buffer, "Failed to install metadata");
        ili9341_write(ili9341, pax_buffer->buf);
        wait_for_button(button_queue);
        return false;
    }
    fwrite(data_app_info, 1, size_app_info, metadata_fd);
    fclose(metadata_fd);

    ESP_LOGI(TAG, "App installed!");
    render_message(pax_buffer, "App has been installed!");
    ili9341_write(ili9341, pax_buffer->buf);
    wait_for_button(button_queue);
    return true;
}

bool menu_hatchery_install_app(xQueueHandle button_queue, pax_buf_t *pax_buffer, ILI9341 *ili9341, const char* type_slug, const char* category_slug, const char* app_slug) {
    cJSON* slug_obj = cJSON_GetObjectItem(json_app_info, "slug");
    cJSON* name_obj = cJSON_GetObjectItem(json_app_info, "name");
    cJSON* author_obj = cJSON_GetObjectItem(json_app_info, "author");
    cJSON* license_obj = cJSON_GetObjectItem(json_app_info, "license");
    cJSON* description_obj = cJSON_GetObjectItem(json_app_info, "description");
    cJSON* version_obj = cJSON_GetObjectItem(json_app_info, "version");
    cJSON* files_obj = cJSON_GetObjectItem(json_app_info, "files");

    uint64_t size_fat = 0;
    uint64_t size_appfs = 0;

    cJSON* file_obj;
    cJSON_ArrayForEach(file_obj, files_obj) {
        cJSON* name_obj = cJSON_GetObjectItem(file_obj, "name");
        cJSON* size_obj = cJSON_GetObjectItem(file_obj, "size");
        uint64_t size = size_obj->valueint;
        if ((strcmp(type_slug, esp32_type) == 0) && (strcmp(name_obj->valuestring, esp32_bin_fn))) {
            size_appfs += size;
        } else {
            size_fat += size;
        }
    }

    printf("App selected: %s (version %u) by %s. Size: %llu, Appfs size: %llu\r\n", name_obj->valuestring, version_obj->valueint, author_obj->valuestring, size_fat, size_appfs);

    uint64_t fs_free;
    get_internal_filesystem_size_and_available(NULL, &fs_free);
    bool can_install_to_internal = (fs_free >= size_fat);
    bool can_install_to_sdcard = false;

    if (get_sdcard_mounted()) {
        get_sdcard_filesystem_size_and_available(NULL, &fs_free);
        can_install_to_sdcard = (fs_free >= size_fat);
    }

    bool can_install_to_appfs = appfsGetFreeMem() >= size_appfs;

    if (!can_install_to_appfs) {
        render_message(pax_buffer, "Can not install app\nNot enough space available\non the AppFS filesystem");
        ili9341_write(ili9341, pax_buffer->buf);
        wait_for_button(button_queue);
        return false;
    }

    if ((!can_install_to_internal) && (!can_install_to_sdcard)) {
        render_message(pax_buffer, "Can not install app\nNot enough space available\non the FAT filesystem");
        ili9341_write(ili9341, pax_buffer->buf);
        wait_for_button(button_queue);
        return false;
    }

    menu_t *menu = menu_alloc("Install to", 20, 18);
    menu->fgColor           = 0xFF000000;
    menu->bgColor           = 0xFFFFFFFF;
    menu->bgTextColor       = 0xFFFFFFFF;
    menu->selectedItemColor = 0xFFfa448c;
    menu->borderColor       = 0xFF491d88;
    menu->titleColor        = 0xFFfa448c;
    menu->titleBgColor      = 0xFF491d88;
    menu->scrollbarBgColor  = 0xFFCCCCCC;
    menu->scrollbarFgColor  = 0xFF555555;

    if (can_install_to_internal) menu_insert_item(menu, "Internal memory", NULL, (void*) 0, -1);
    if (can_install_to_sdcard) menu_insert_item(menu, "SD card", NULL, (void*) 1, -1);
    menu_insert_item(menu, "Cancel", NULL, (void*) 2, -1);

    bool render = true;
    bool quit = false;
    bool result = false;
    while (!quit) {
        if (render) {
            menu_render(pax_buffer, menu, (pax_buffer->width / 2) - 10, (pax_buffer->height / 2) - 10, (pax_buffer->width / 2), (pax_buffer->height / 2));
            ili9341_write(ili9341, pax_buffer->buf);
            render = false;
        }

        int button = wait_for_button_press(button_queue, portMAX_DELAY);
        switch (button) {
            case RP2040_INPUT_JOYSTICK_DOWN:
                menu_navigate_next(menu);
                render = true;
                break;
            case RP2040_INPUT_JOYSTICK_UP:
                menu_navigate_previous(menu);
                render = true;
                break;
            case RP2040_INPUT_BUTTON_ACCEPT:
            case RP2040_INPUT_JOYSTICK_PRESS:
            case RP2040_INPUT_BUTTON_START: {
                int action = (int) menu_get_callback_args(menu, menu_get_position(menu));
                switch (action) {
                    case 0:
                        result = menu_hatchery_install_app_execute(button_queue, pax_buffer, ili9341, type_slug, category_slug, app_slug, false);
                        break;
                    case 1:
                        result = menu_hatchery_install_app_execute(button_queue, pax_buffer, ili9341, type_slug, category_slug, app_slug, true);
                        break;
                    case 2:
                    default:
                        break;
                }
                quit = true;
                break;
            }
            case RP2040_INPUT_BUTTON_BACK:
            case RP2040_INPUT_BUTTON_SELECT:
            case RP2040_INPUT_BUTTON_MENU:
            case RP2040_INPUT_BUTTON_HOME:
                quit = true;
                break;
            default:
                break;
        }
    }

    menu_free(menu);
    return result;
}

bool menu_hatchery_app_info(xQueueHandle button_queue, pax_buf_t *pax_buffer, ILI9341 *ili9341, const char* type_slug, const char* category_slug, const char* app_slug) {
    display_busy(pax_buffer, ili9341);
    if (!load_app_info(type_slug, category_slug, app_slug)) {
        show_communication_error(button_queue, pax_buffer, ili9341);
        return false;
    }

    cJSON* slug_obj = cJSON_GetObjectItem(json_app_info, "slug");
    cJSON* name_obj = cJSON_GetObjectItem(json_app_info, "name");
    cJSON* author_obj = cJSON_GetObjectItem(json_app_info, "author");
    cJSON* license_obj = cJSON_GetObjectItem(json_app_info, "license");
    cJSON* description_obj = cJSON_GetObjectItem(json_app_info, "description");
    cJSON* version_obj = cJSON_GetObjectItem(json_app_info, "version");
    cJSON* files_obj = cJSON_GetObjectItem(json_app_info, "files");

    bool render = true;
    bool quit = false;
    while (!quit) {
        if (render) {
            pax_background(pax_buffer, 0xFFFFFF);
            render_header(pax_buffer, 0, 0, pax_buffer->width, 34, 18, 0xFFfa448c, 0xFF491d88, NULL, name_obj->valuestring);
            char buffer[128];
            snprintf(buffer, sizeof(buffer) - 1, "Author: %s", author_obj->valuestring);
            pax_draw_text(pax_buffer, 0xFF491d88, pax_font_saira_regular, 18, 5, 52, buffer);
            snprintf(buffer, sizeof(buffer) - 1, "License: %s", license_obj->valuestring);
            pax_draw_text(pax_buffer, 0xFF491d88, pax_font_saira_regular, 18, 5, 52 + 20 * 1, buffer);
            snprintf(buffer, sizeof(buffer) - 1, "Version: %u", version_obj->valueint);
            pax_draw_text(pax_buffer, 0xFF491d88, pax_font_saira_regular, 18, 5, 52 + 20 * 2, buffer);
            pax_draw_text(pax_buffer, 0xFF491d88, pax_font_saira_regular, 18, 5, 52 + 20 * 3, description_obj->valuestring);
            pax_draw_text(pax_buffer, 0xFF491d88, pax_font_saira_regular, 18, 5, pax_buffer->height - 18, "🅰 install app  🅱 back");
            ili9341_write(ili9341, pax_buffer->buf);
            render = false;
        }

        int button = wait_for_button_press(button_queue, portMAX_DELAY);
        switch (button) {
            case RP2040_INPUT_JOYSTICK_DOWN:
                render = true;
                break;
            case RP2040_INPUT_JOYSTICK_UP:
                render = true;
                break;
            case RP2040_INPUT_BUTTON_ACCEPT:
            case RP2040_INPUT_JOYSTICK_PRESS:
            case RP2040_INPUT_BUTTON_START:
                render = true;
                menu_hatchery_install_app(button_queue, pax_buffer, ili9341, type_slug, category_slug, app_slug);
                break;
            case RP2040_INPUT_BUTTON_BACK:
                quit = true;
                break;
            case RP2040_INPUT_BUTTON_SELECT:
                quit = true;
                break;
            case RP2040_INPUT_BUTTON_MENU:
                quit = true;
                break;
            case RP2040_INPUT_BUTTON_HOME:
                quit = true;
                break;
            default:
                break;
        }
    }

    hatchery_free_app_info();
    return true;
}

bool menu_hatchery_apps(xQueueHandle button_queue, pax_buf_t *pax_buffer, ILI9341 *ili9341, const char* type_slug, const char* category_slug) {
    display_busy(pax_buffer, ili9341);
    if (!load_apps(type_slug, category_slug)) {
        show_communication_error(button_queue, pax_buffer, ili9341);
        return false;
    }

    menu_t* menu = hatchery_menu_create("Apps");

    cJSON* app_obj;
    cJSON_ArrayForEach(app_obj, json_apps) {
        cJSON* slug_obj = cJSON_GetObjectItem(app_obj, "slug");
        cJSON* name_obj = cJSON_GetObjectItem(app_obj, "name");
        cJSON* author_obj = cJSON_GetObjectItem(app_obj, "author");
        cJSON* license_obj = cJSON_GetObjectItem(app_obj, "license");
        cJSON* description_obj = cJSON_GetObjectItem(app_obj, "description");
        cJSON* version_obj = cJSON_GetObjectItem(app_obj, "version");
        menu_insert_item(menu, name_obj->valuestring, NULL, (void*) slug_obj->valuestring, -1);
    }

    bool quit = false;
    while (!quit) {
        const char* app_slug = (const char*) hatchery_menu_show(button_queue, pax_buffer, ili9341, menu, "🅰 select app  🅱 back", &quit, NULL, NULL, &quit);
        if (quit) break;
        quit = !menu_hatchery_app_info(button_queue, pax_buffer, ili9341, type_slug, category_slug, app_slug);
    }

    hatchery_menu_destroy(menu);
    hatchery_free_apps();
    return true;
}

bool menu_hatchery_categories(xQueueHandle button_queue, pax_buf_t *pax_buffer, ILI9341 *ili9341, const char* type_slug) {
    display_busy(pax_buffer, ili9341);
    if (!load_categories(type_slug)) {
        show_communication_error(button_queue, pax_buffer, ili9341);
        return false;
    }

    menu_t* menu = hatchery_menu_create("Categories");

    cJSON* category_obj;
    cJSON_ArrayForEach(category_obj, json_categories) {
        cJSON* slug_obj = cJSON_GetObjectItem(category_obj, "slug");
        cJSON* name_obj = cJSON_GetObjectItem(category_obj, "name");
        cJSON* apps_obj = cJSON_GetObjectItem(category_obj, "apps");
        menu_insert_item(menu, name_obj->valuestring, NULL, (void*) slug_obj->valuestring, -1);
    }

    bool quit = false;
    while (!quit) {
        const char* category_slug = (const char*) hatchery_menu_show(button_queue, pax_buffer, ili9341, menu, "🅰 select category  🅱 back", &quit, NULL, NULL, &quit);
        if (quit) break;
        quit = !menu_hatchery_apps(button_queue, pax_buffer, ili9341, type_slug, category_slug);
    }

    hatchery_menu_destroy(menu);
    hatchery_free_categories();
    return true;
}

void menu_hatchery(xQueueHandle button_queue, pax_buf_t *pax_buffer, ILI9341 *ili9341) {
    display_busy(pax_buffer, ili9341);
    if (!connect_to_wifi(button_queue, pax_buffer, ili9341)) return;
    if (!load_types()) {
        wifi_disconnect_and_disable();
        hatchery_free();
        show_communication_error(button_queue, pax_buffer, ili9341);
        return;
    }

    menu_t* menu = hatchery_menu_create("Hatchery");

    cJSON* type_obj;
    cJSON_ArrayForEach(type_obj, json_types) {
        cJSON* slug_obj = cJSON_GetObjectItem(type_obj, "slug");
        cJSON* name_obj = cJSON_GetObjectItem(type_obj, "name");
        menu_insert_item(menu, name_obj->valuestring, NULL, (void*) slug_obj->valuestring, -1);
    }

    bool quit = false;
    while (!quit) {
        const char* type_slug = (const char*) hatchery_menu_show(button_queue, pax_buffer, ili9341, menu, "🅰 select type  🅱 back", &quit, NULL, NULL, &quit);
        if (quit) break;
        quit = !menu_hatchery_categories(button_queue, pax_buffer, ili9341, type_slug);
    }

    hatchery_menu_destroy(menu);
    wifi_disconnect_and_disable();
    hatchery_free();
}
