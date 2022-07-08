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

#include "app_management.h"
#include "appfs.h"
#include "appfs_wrapper.h"
#include "bootscreen.h"
#include "fpga_download.h"
#include "fpga_util.h"
#include "graphics_wrapper.h"
#include "hardware.h"
#include "http_download.h"
#include "ice40.h"
#include "ili9341.h"
#include "launcher.h"
#include "menu.h"
#include "metadata.h"
#include "pax_codecs.h"
#include "pax_gfx.h"
#include "rp2040.h"
#include "rtc_memory.h"
#include "system_wrapper.h"
#include "wifi_connect.h"

static const char* TAG = "Updater";

typedef struct _update_apps_callback_args {
    xQueueHandle button_queue;
    pax_buf_t*   pax_buffer;
    ILI9341*     ili9341;
    bool         sdcard;
} update_apps_callback_args_t;

static bool connect_to_wifi(xQueueHandle button_queue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    if (!wifi_connect_to_stored()) {
        wifi_disconnect_and_disable();
        render_message(pax_buffer, "Unable to connect to\nthe WiFi network");
        ili9341_write(ili9341, pax_buffer->buf);
        wait_for_button(button_queue);
        return false;
    }
    return true;
}

static char*  data_app_info = NULL;
static size_t size_app_info = 0;
static cJSON* json_app_info = NULL;

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

static void free_app_info() {
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

#define AMOUNT_OF_LINES 8
char* terminal_lines[AMOUNT_OF_LINES] = {NULL};

static void terminal_render(pax_buf_t* pax_buffer, ILI9341* ili9341) {
    pax_background(pax_buffer, 0xFFFFFF);
    render_header(pax_buffer, 0, 0, pax_buffer->width, 34, 18, 0xFFfa448c, 0xFF491d88, NULL, "Updating apps...");
    uint8_t printed_lines = 0;
    for (uint8_t line = 0; line < AMOUNT_OF_LINES; line++) {
        if (terminal_lines[line] != NULL) {
            pax_draw_text(pax_buffer, 0xFF491d88, pax_font_saira_regular, 18, 2, 48 + 20 * printed_lines, terminal_lines[line]);
            printed_lines++;
        }
    }
    ili9341_write(ili9341, pax_buffer->buf);
}

static void terminal_add(char* buffer) {
    if (terminal_lines[0] != NULL) {
        free(terminal_lines[0]);
        terminal_lines[0] = NULL;
    }
    for (uint8_t i = 0; i < AMOUNT_OF_LINES - 1; i++) {
        terminal_lines[i] = terminal_lines[i + 1];
    }
    terminal_lines[AMOUNT_OF_LINES - 1] = buffer;
}

static void terminal_free() {
    for (uint8_t i = 0; i < AMOUNT_OF_LINES - 1; i++) {
        if (terminal_lines[i] != NULL) {
            free(terminal_lines[i]);
            terminal_lines[i] = NULL;
        }
    }
}

static void callback(const char* path, const char* entity, void* user) {
    update_apps_callback_args_t* args = (update_apps_callback_args_t*) user;
    char                         string_buffer[128];
    string_buffer[sizeof(string_buffer) - 1] = '\0';
    char metadata_path[128];
    metadata_path[sizeof(metadata_path) - 1] = '\0';
    snprintf(metadata_path, sizeof(metadata_path) - 1, "%s/%s/metadata.json", path, entity);

    char* device            = NULL;
    char* type              = NULL;
    char* category          = NULL;
    char* slug              = NULL;
    char* name              = NULL;
    int   installed_version = 0;
    parse_metadata(metadata_path, &device, &type, &category, &slug, &name, NULL, NULL, &installed_version, NULL);

    if ((slug == NULL) || (strcmp(slug, entity) != 0)) {
        snprintf(string_buffer, sizeof(string_buffer) - 1, "%s/%s: no metadata", path, entity);
        ESP_LOGW(TAG, "%s", string_buffer);
        terminal_add(strdup(string_buffer));
        terminal_render(args->pax_buffer, args->ili9341);
        goto end;
    }

    if ((type == NULL) || (category == NULL)) {
        snprintf(string_buffer, sizeof(string_buffer) - 1, "%s: incomplete metadata", slug);
        ESP_LOGW(TAG, "%s", string_buffer);
        terminal_add(strdup(string_buffer));
        terminal_render(args->pax_buffer, args->ili9341);
        goto end;
    }

    if (!load_app_info(type, category, slug)) {
        snprintf(string_buffer, sizeof(string_buffer) - 1, "%s: fetching metadata failed", slug);
        ESP_LOGW(TAG, "%s", string_buffer);
        terminal_add(strdup(string_buffer));
        terminal_render(args->pax_buffer, args->ili9341);
        goto end;
    }

    cJSON* version_obj = cJSON_GetObjectItem(json_app_info, "version");

    if (!version_obj) {
        snprintf(string_buffer, sizeof(string_buffer) - 1, "%s: hatchery has no version", slug);
        ESP_LOGW(TAG, "%s", string_buffer);
        terminal_add(strdup(string_buffer));
        terminal_render(args->pax_buffer, args->ili9341);
        goto end;
    }

    if (installed_version >= version_obj->valueint) {
        snprintf(string_buffer, sizeof(string_buffer) - 1, "%s: up to date", slug);
        ESP_LOGW(TAG, "%s", string_buffer);
        terminal_add(strdup(string_buffer));
        terminal_render(args->pax_buffer, args->ili9341);
        goto end;
    }

    snprintf(string_buffer, sizeof(string_buffer) - 1, "%s: r%d to r%d", slug, installed_version, version_obj->valueint);
    ESP_LOGI(TAG, "%s", string_buffer);
    terminal_add(strdup(string_buffer));
    terminal_render(args->pax_buffer, args->ili9341);

    if (install_app(NULL, args->pax_buffer, args->ili9341, type, args->sdcard, data_app_info, size_app_info, json_app_info)) {
        snprintf(string_buffer, sizeof(string_buffer) - 1, "%s: installed r%d", slug, version_obj->valueint);
        ESP_LOGI(TAG, "%s", string_buffer);
    } else {
        snprintf(string_buffer, sizeof(string_buffer) - 1, "%s: failed to install", slug);
        ESP_LOGE(TAG, "%s", string_buffer);
    }

    terminal_add(strdup(string_buffer));
    terminal_render(args->pax_buffer, args->ili9341);

end:
    free_app_info();
    if (device != NULL) free(device);
    if (type != NULL) free(type);
    if (slug != NULL) free(slug);
    if (category != NULL) free(category);
    if (name != NULL) free(name);
}

void update_apps(xQueueHandle button_queue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    terminal_add(strdup("Connecting to WiFi..."));
    terminal_render(pax_buffer, ili9341);

    if (!connect_to_wifi(button_queue, pax_buffer, ili9341)) return;

    terminal_add(strdup("Connected to WiFi!"));
    terminal_render(pax_buffer, ili9341);

    update_apps_callback_args_t args;
    args.button_queue = button_queue;
    args.pax_buffer   = pax_buffer;
    args.ili9341      = ili9341;
    args.sdcard       = false;

    for_entity_in_path("/internal/apps/esp32", true, &callback, &args);
    for_entity_in_path("/internal/apps/python", true, &callback, &args);
    for_entity_in_path("/internal/apps/ice40", true, &callback, &args);
    args.sdcard = true;
    for_entity_in_path("/sd/apps/esp32", true, &callback, &args);
    for_entity_in_path("/sd/apps/python", true, &callback, &args);
    for_entity_in_path("/sd/apps/ice40", true, &callback, &args);

    terminal_free();
}
