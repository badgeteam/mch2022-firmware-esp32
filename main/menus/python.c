#include "python.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>
#include <esp_vfs.h>
#include <esp_vfs_fat.h>
#include <cJSON.h>

#include "hardware.h"
#include "ili9341.h"
#include "menu.h"
#include "pax_codecs.h"
#include "pax_gfx.h"
#include "rp2040.h"
#include "rtc_memory.h"
#include "appfs.h"
#include "appfs_wrapper.h"
#include "system_wrapper.h"

extern const uint8_t python_png_start[] asm("_binary_python_png_start");
extern const uint8_t python_png_end[] asm("_binary_python_png_end");

static appfs_handle_t python_appfs_fd = APPFS_INVALID_FD;

typedef enum action {
    ACTION_NONE,
    ACTION_TEST
} menu_python_action_t;

void start_python_app(const char* application) {
    rtc_memory_string_write(application);
    appfs_boot_app(python_appfs_fd);
}

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

void populate_menu_entry_from_path(menu_t* menu, const char* path, const char* name) { // Path is here the folder of a specific app, for example /internal/apps/event_schedule
    char metadata_file_path[128];
    snprintf(metadata_file_path, sizeof(metadata_file_path), "%s/%s/metadata.json", path, name);
    char init_file_path[128];
    snprintf(init_file_path, sizeof(init_file_path), "%s/%s/__init__.py", path, name);
    char icon_file_path[128];
    snprintf(icon_file_path, sizeof(icon_file_path), "%s/%s/icon.png", path, name);
    
    printf("Processing Python app %s/%s\n", path, name);
    printf("%s\n", metadata_file_path);
    printf("%s\n", init_file_path);
    printf("%s\n", icon_file_path);

    char* title = NULL;
    char* description = NULL;
    char* category = NULL;
    char* author = NULL;
    int revision = -1;
    
    parse_metadata(metadata_file_path, &title, &description, &category, &author, &revision);
    
    if (title != NULL) printf("Name: %s\n", title);
    if (description != NULL) printf("Description: %s\n", description);
    if (category != NULL) printf("Category: %s\n", category);
    if (author != NULL) printf("Author: %s\n", author);
    if (revision >= 0) printf("Revision: %u\n", revision);
    
    pax_buf_t* icon = NULL;
    
    FILE* icon_fd = fopen(icon_file_path, "rb");
    if (icon_fd != NULL) {
        size_t icon_size = get_file_size(icon_fd);
        uint8_t* icon_data = load_file_to_ram(icon_fd);
        if (icon_data != NULL) {
            icon = malloc(sizeof(pax_buf_t));
            if (icon != NULL) {
                pax_decode_png_buf(icon, (void*) icon_data, icon_size, PAX_BUF_32_8888ARGB, 0);
            }
            free(icon_data);
        }
    }

    menu_insert_item_icon(menu, (title != NULL) ? title : name, NULL, (void*) name, -1, icon);
    
    if (title) free(title);
    if (description) free(description);
    if (category) free(category);
    if (author) free(author);
}

bool populate_menu_from_path(menu_t* menu, const char* path) { // Path is here the folder containing the Python apps, for example /internal/apps
    DIR* dir = opendir(path);
    if (dir == NULL) {
        printf("Failed to populate menu, directory not found: %s\n", path);
        return false;
    }
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type == DT_REG) continue; // Skip files, only parse directories
        populate_menu_entry_from_path(menu, path, ent->d_name);
    }
    closedir(dir);
    return true;
}

bool populate_menu(menu_t* menu) {
    bool internal_result = populate_menu_from_path(menu, "/internal/apps");
    bool sdcard_result = populate_menu_from_path(menu, "/sd/apps");
    return internal_result | sdcard_result;
}

void menu_python(xQueueHandle button_queue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    python_appfs_fd = appfsOpen("python");
    
    if (python_appfs_fd == APPFS_INVALID_FD) {
        pax_noclip(pax_buffer);
        const pax_font_t* font = pax_get_font("saira regular");
        pax_background(pax_buffer, 0xFFFFFF);
        pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 0, "BadgePython not installed!\nPlease install BadgePython\nusing the Hatchery.\n\nPress A or B to return.");
        ili9341_write(ili9341, pax_buffer->buf);
        wait_for_button(button_queue);
        return;
    }
    
    menu_t* menu = menu_alloc("BadgePython apps", 34, 18);

    menu->fgColor           = 0xFF000000;
    menu->bgColor           = 0xFFFFFFFF;
    menu->bgTextColor       = 0xFF000000;
    menu->selectedItemColor = 0xFFfec859;
    menu->borderColor       = 0xFFfa448c;
    menu->titleColor        = 0xFFfec859;
    menu->titleBgColor      = 0xFFfa448c;
    menu->scrollbarBgColor  = 0xFFCCCCCC;
    menu->scrollbarFgColor  = 0xFF555555;

    pax_buf_t icon_python;
    pax_decode_png_buf(&icon_python, (void*) python_png_start, python_png_end - python_png_start, PAX_BUF_32_8888ARGB, 0);
    menu_set_icon(menu, &icon_python);

    populate_menu(menu);
    menu_insert_item(menu, "Python Hatchery", NULL, (void*) strdup("dashboard.installer"), -1);
    menu_insert_item(menu, "Home", NULL, (void*) strdup("dashboard.home"), -1);
    menu_insert_item(menu, "Launcher", NULL, (void*) strdup("dashboard.launcher"), -1);
    menu_insert_item(menu, "About", NULL, (void*) strdup("dashboard.other.about"), -1);

    char* app_to_start = NULL;
    bool render = true;
    bool render_help = true;
    bool quit = false;
    while (!quit) {
        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(button_queue, &buttonMessage, 16 / portTICK_PERIOD_MS) == pdTRUE) {
            if (buttonMessage.state) {
                switch (buttonMessage.input) {
                    case RP2040_INPUT_JOYSTICK_DOWN:
                            menu_navigate_next(menu);
                            render = true;
                        break;
                    case RP2040_INPUT_JOYSTICK_UP:
                            menu_navigate_previous(menu);
                            render = true;
                        break;
                    case RP2040_INPUT_BUTTON_HOME:
                    case RP2040_INPUT_BUTTON_BACK:
                        quit = true;
                        break;
                    case RP2040_INPUT_BUTTON_ACCEPT:
                    case RP2040_INPUT_JOYSTICK_PRESS:
                    case RP2040_INPUT_BUTTON_SELECT:
                    case RP2040_INPUT_BUTTON_START:
                        app_to_start = (char*) menu_get_callback_args(menu, menu_get_position(menu));
                        break;
                    default:
                        break;
                }
            }
        }
        
        if (render_help) {
            const pax_font_t* font = pax_get_font("saira regular");
            pax_background(pax_buffer, 0xFFFFFF);
            pax_noclip(pax_buffer);
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "[A] start app [B] back");
            render_help = false;
        }

        if (render) {
            menu_render(pax_buffer, menu, 0, 0, 320, 220, 0xFF491d88);
            ili9341_write(ili9341, pax_buffer->buf);
            render = false;
        }

        if (app_to_start != NULL) {
            start_python_app(app_to_start);
            app_to_start = NULL;
            render = true;
            render_help = true;
        }
    }
    
    for (size_t index = 0; index < menu_get_length(menu); index++) {
        pax_buf_t* icon = menu_get_icon(menu, index);
        if (icon != NULL) pax_buf_destroy(icon);
        free(menu_get_callback_args(menu, index));
    }

    menu_free(menu);
    pax_buf_destroy(&icon_python);
}
