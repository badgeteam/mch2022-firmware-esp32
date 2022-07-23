#include "launcher.h"

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

#include "appfs.h"
#include "appfs_wrapper.h"
#include "bootscreen.h"
#include "fpga_download.h"
#include "fpga_util.h"
#include "graphics_wrapper.h"
#include "hardware.h"
#include "ice40.h"
#include "ili9341.h"
#include "menu.h"
#include "metadata.h"
#include "pax_codecs.h"
#include "pax_gfx.h"
#include "rp2040.h"
#include "rtc_memory.h"
#include "system_wrapper.h"

static const char* TAG = "Launcher";

extern const uint8_t apps_png_start[] asm("_binary_apps_png_start");
extern const uint8_t apps_png_end[] asm("_binary_apps_png_end");

extern const uint8_t bitstream_png_start[] asm("_binary_bitstream_png_start");
extern const uint8_t bitstream_png_end[] asm("_binary_bitstream_png_end");

extern const uint8_t python_png_start[] asm("_binary_python_png_start");
extern const uint8_t python_png_end[] asm("_binary_python_png_end");

extern const uint8_t dev_png_start[] asm("_binary_dev_png_start");
extern const uint8_t dev_png_end[] asm("_binary_dev_png_end");

static appfs_handle_t python_appfs_fd      = APPFS_INVALID_FD;
static bool           python_not_installed = false;

static void start_fpga_app(xQueueHandle button_queue, pax_buf_t* pax_buffer, ILI9341* ili9341, const char* path) {
    const pax_font_t* font = pax_font_saira_regular;
    char              filename[128];
    snprintf(filename, sizeof(filename), "%s/bitstream.bin", path);
    FILE* fd = fopen(filename, "rb");
    if (fd == NULL) {
        pax_background(pax_buffer, 0xFFFFFF);
        pax_draw_text(pax_buffer, 0xFFFF0000, font, 18, 0, 0, "Failed to open file\n\nPress A or B to go back");
        ili9341_write(ili9341, pax_buffer->buf);
        wait_for_button(button_queue);
        return;
    }
    size_t   bitstream_length = get_file_size(fd);
    uint8_t* bitstream        = load_file_to_ram(fd);
    ICE40*   ice40            = get_ice40();
    ili9341_deinit(ili9341);
    ili9341_select(ili9341, false);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    ili9341_select(ili9341, true);
    esp_err_t res = ice40_load_bitstream(ice40, bitstream, bitstream_length);
    free(bitstream);
    fclose(fd);
    if (res == ESP_OK) {
        fpga_irq_setup(ice40);
        fpga_host(button_queue, ice40, pax_buffer, ili9341, false, path);
        fpga_irq_cleanup(ice40);
        ice40_disable(ice40);
        ili9341_init(ili9341);
    } else {
        ice40_disable(ice40);
        ili9341_init(ili9341);
        pax_background(pax_buffer, 0xFFFFFF);
        pax_draw_text(pax_buffer, 0xFFFF0000, font, 18, 0, 0, "Failed to load bitstream\n\nPress A or B to go back");
        ili9341_write(ili9341, pax_buffer->buf);
        wait_for_button(button_queue);
    }
}

static void start_python_app(const char* path) {
    rtc_memory_string_write(path);
    appfs_boot_app(python_appfs_fd);
}

static bool find_menu_item_for_type_and_slug(menu_t* menu, const char* type, const char* slug) {
    for (size_t index = 0; index < menu_get_length(menu); index++) {
        launcher_app_t* app = (launcher_app_t*) menu_get_callback_args(menu, index);
        if ((strlen(app->type) == strlen(type)) && (strcmp(app->type, type) == 0)) {
            if ((strlen(app->slug) == strlen(slug)) && (strcmp(app->slug, slug) == 0)) {
                return true;
            }
        }
    }
    return false;
}

static bool populate_menu_from_other_appfs_entries(menu_t* menu) {
    bool            empty = true;
    launcher_app_t* other_apps[64];
    size_t          other_apps_count = 0;
    appfs_handle_t  appfs_fd         = appfsNextEntry(APPFS_INVALID_FD);
    while (appfs_fd != APPFS_INVALID_FD) {
        if (other_apps_count >= sizeof(other_apps) - 1) break;  // Prevent overflow
        empty               = false;
        const char* slug    = NULL;
        const char* title   = NULL;
        uint16_t    version = 0xFFFF;
        appfsEntryInfoExt(appfs_fd, &slug, &title, &version, NULL);

        if (!find_menu_item_for_type_and_slug(menu, "esp32", slug)) {
            // AppFS entry has no metadata installed, create a simple menu entry anyway
            launcher_app_t* app = malloc(sizeof(launcher_app_t));
            if (app != NULL) {
                memset(app, 0, sizeof(launcher_app_t));
                app->appfs_fd = appfs_fd;
                app->type     = strdup("esp32");
                app->slug     = strdup(slug);
                app->title    = strdup(title);
                app->version  = version;
                app->icon     = malloc(sizeof(pax_buf_t));
                if (app->icon != NULL) {
                    pax_decode_png_buf(app->icon, (void*) dev_png_start, dev_png_end - dev_png_start, PAX_BUF_32_8888ARGB, 0);
                }
                other_apps[other_apps_count++] = app;
            }
        }
        appfs_fd = appfsNextEntry(appfs_fd);
    }

    for (size_t index = 0; index < other_apps_count; index++) {
        launcher_app_t* app = other_apps[index];
        menu_insert_item_icon(menu, (app->title != NULL) ? app->title : app->slug, NULL, (void*) app, -1, app->icon);
    }

    return !empty;
}

static bool populate_menu(menu_t* menu) {
    bool internal_result_esp32 = populate_menu_from_path(menu, "/internal/apps", "esp32", (void*) apps_png_start, apps_png_end - apps_png_start);
    bool sdcard_result_esp32   = populate_menu_from_path(menu, "/sd/apps", "esp32", (void*) apps_png_start, apps_png_end - apps_png_start);
    bool other_result_esp32    = populate_menu_from_other_appfs_entries(menu);  // Keep this one last!
    bool internal_result_ice40 = populate_menu_from_path(menu, "/internal/apps", "ice40", (void*) bitstream_png_start, bitstream_png_end - bitstream_png_start);
    bool sdcard_result_ice40   = populate_menu_from_path(menu, "/sd/apps", "ice40", (void*) bitstream_png_start, bitstream_png_end - bitstream_png_start);
    bool internal_result_python = populate_menu_from_path(menu, "/internal/apps", "python", (void*) python_png_start, python_png_end - python_png_start);
    bool sdcard_result_python   = populate_menu_from_path(menu, "/sd/apps", "python", (void*) python_png_start, python_png_end - python_png_start);
    return internal_result_esp32 | sdcard_result_esp32 | other_result_esp32 | internal_result_ice40 | sdcard_result_ice40 | internal_result_python |
           sdcard_result_python;
}

static void start_app(xQueueHandle button_queue, pax_buf_t* pax_buffer, ILI9341* ili9341, launcher_app_t* app_to_start) {
    display_boot_screen(pax_buffer, ili9341, "Starting app...");
    if ((strlen(app_to_start->type) == strlen("ice40")) && (strncmp(app_to_start->type, "ice40", strlen(app_to_start->type)) == 0)) {
        start_fpga_app(button_queue, pax_buffer, ili9341, app_to_start->path);
    } else if ((strlen(app_to_start->type) == strlen("python")) && (strncmp(app_to_start->type, "python", strlen(app_to_start->type)) == 0)) {
        if (python_not_installed) {
            render_message(pax_buffer, "Python is not installed\n\nPlease install 'Python'\nusing the Hatchery under\n'ESP32 native binaries\\Utility'");
            ili9341_write(ili9341, pax_buffer->buf);
            wait_for_button(button_queue);
        } else {
            start_python_app(app_to_start->path);
        }
    } else if ((strlen(app_to_start->type) == strlen("esp32")) && (strncmp(app_to_start->type, "esp32", strlen(app_to_start->type)) == 0)) {
        appfs_handle_t appfs_fd_to_start = app_to_start->appfs_fd;
        uint16_t       version_in_appfs  = 0xFFFF;
        appfsEntryInfoExt(appfs_fd_to_start, NULL, NULL, &version_in_appfs, NULL);
        if (app_to_start->version != version_in_appfs) {
            // TO DO: update the AppFS entry from FAT if possible
            ESP_LOGE(TAG, "Revision in AppFS: %u, version in metadata: %u", version_in_appfs, app_to_start->version);
            render_message(pax_buffer, "Warning:\nAppFS entry version does not\nmatch version in app metadata");
            ili9341_write(ili9341, pax_buffer->buf);
            wait_for_button(button_queue);
            appfs_boot_app(appfs_fd_to_start);  // Start anyway
        } else if (appfs_fd_to_start != APPFS_INVALID_FD) {
            appfs_boot_app(appfs_fd_to_start);
        } else {
            // TO DO: install the AppFS entry from FAT if possible
            render_message(pax_buffer, "AppFS entry not found");
            ili9341_write(ili9341, pax_buffer->buf);
            wait_for_button(button_queue);
        }
    } else {
        render_message(pax_buffer, "Unknown application type!");
        ili9341_write(ili9341, pax_buffer->buf);
        ESP_LOGE(TAG, "Unknown application type: %s", app_to_start->type);
        wait_for_button(button_queue);
    }
}

static bool uninstall_app(xQueueHandle button_queue, pax_buf_t* pax_buffer, ILI9341* ili9341, launcher_app_t* app) {
    render_message(pax_buffer, "Uninstalling...");
    ili9341_write(ili9341, pax_buffer->buf);

    // 1) Remove AppFS entry
    if (app->appfs_fd != APPFS_INVALID_FD) {
        printf("Removing AppFS entry...\r\n");
        const char* slug = NULL;
        appfsEntryInfoExt(app->appfs_fd, &slug, NULL, NULL, NULL);
        appfsDeleteFile(slug);
    }

    // 2) Remove data from filesystem
    if (app->path != NULL) {
        printf("Removing directory '%s'...\r\n", app->path);
        remove_recursive(app->path);
    }

    return true;
}

static bool show_app_details(xQueueHandle button_queue, pax_buf_t* pax_buffer, ILI9341* ili9341, launcher_app_t* app) {
    bool return_value = false;
    bool render       = true;
    bool quit         = false;
    while (!quit) {
        if (render) {
            pax_simple_rect(pax_buffer, 0xFFFFFFFF, 0, pax_buffer->height - 20, pax_buffer->width, 20);
            pax_draw_text(pax_buffer, 0xFF491d88, pax_font_saira_regular, 18, 5, pax_buffer->height - 18, "🅰 start  🅱 back  🅴 uninstall");
            render_outline(pax_buffer, 0, 0, pax_buffer->width, pax_buffer->height - 20, 0xFFfa448c, 0xFFFFFFFF);
            render_header(pax_buffer, 0, 0, pax_buffer->width, 34, 18, 0xFFfec859, 0xFFfa448c, app->icon, app->title);
            char buffer[128];
            snprintf(buffer, sizeof(buffer) - 1, "Type: %s", (app->type != NULL) ? app->type : "Unknown");
            pax_draw_text(pax_buffer, 0xFF491d88, pax_font_saira_regular, 18, 5, 48 + 20 * 0, buffer);
            snprintf(buffer, sizeof(buffer) - 1, "Author: %s", (app->author != NULL) ? app->author : "Unknown");
            pax_draw_text(pax_buffer, 0xFF491d88, pax_font_saira_regular, 18, 5, 48 + 20 * 1, buffer);
            snprintf(buffer, sizeof(buffer) - 1, "License: %s", (app->license != NULL) ? app->license : "Unknown");
            pax_draw_text(pax_buffer, 0xFF491d88, pax_font_saira_regular, 18, 5, 48 + 20 * 2, buffer);
            snprintf(buffer, sizeof(buffer) - 1, "Version: %u", app->version);
            pax_draw_text(pax_buffer, 0xFF491d88, pax_font_saira_regular, 18, 5, 48 + 20 * 3, buffer);
            pax_draw_text(pax_buffer, 0xFF491d88, pax_font_saira_regular, 18, 5, 48 + 20 * 4, (app->description != NULL) ? app->description : "Unknown");
            ili9341_write(ili9341, pax_buffer->buf);
            render = false;
        }
        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(button_queue, &buttonMessage, portMAX_DELAY) == pdTRUE) {
            if (buttonMessage.state) {
                switch (buttonMessage.input) {
                    case RP2040_INPUT_BUTTON_HOME:
                    case RP2040_INPUT_BUTTON_BACK:
                        quit = true;
                        break;
                    case RP2040_INPUT_JOYSTICK_PRESS:
                    case RP2040_INPUT_BUTTON_ACCEPT:
                    case RP2040_INPUT_BUTTON_START:
                        start_app(button_queue, pax_buffer, ili9341, app);
                        render = true;
                        break;
                    case RP2040_INPUT_BUTTON_SELECT:
                        if (uninstall_app(button_queue, pax_buffer, ili9341, app)) {
                            return_value = true;
                            quit         = true;
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }
    return return_value;
}

void menu_launcher(xQueueHandle button_queue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    size_t ram_before = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    bool   reload     = true;
    while (reload) {
        reload = false;
        display_busy(pax_buffer, ili9341);
        python_appfs_fd      = appfsOpen("python");
        python_not_installed = (python_appfs_fd == APPFS_INVALID_FD);

        menu_t* menu = menu_alloc("App launcher", 34, 18);

        menu->fgColor           = 0xFF000000;
        menu->bgColor           = 0xFFFFFFFF;
        menu->bgTextColor       = 0xFFFFFFFF;
        menu->selectedItemColor = 0xFF491d88;
        menu->borderColor       = 0xFF43b5a0;
        menu->titleColor        = 0xFF491d88;
        menu->titleBgColor      = 0xFF43b5a0;
        menu->scrollbarBgColor  = 0xFFCCCCCC;
        menu->scrollbarFgColor  = 0xFF555555;

        pax_buf_t menu_icon;
        pax_decode_png_buf(&menu_icon, (void*) apps_png_start, apps_png_end - apps_png_start, PAX_BUF_32_8888ARGB, 0);
        menu_set_icon(menu, &menu_icon);

        bool empty = !populate_menu(menu);

        launcher_app_t* app_to_start = NULL;
        bool            render       = true;
        bool            quit         = false;
        while (!quit) {
            if (render) {
                const pax_font_t* font = pax_font_saira_regular;
                pax_background(pax_buffer, 0xFFFFFF);
                pax_noclip(pax_buffer);
                pax_draw_text(pax_buffer, 0xFF491d88, font, 18, 5, 240 - 18, "🅰 start  🅱 back  🅼 options");
                menu_render(pax_buffer, menu, 0, 0, 320, 220);
                if (empty) render_message(pax_buffer, "No apps installed");
                ili9341_write(ili9341, pax_buffer->buf);
                render = false;
            }

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
                        case RP2040_INPUT_BUTTON_START:
                            app_to_start = (launcher_app_t*) menu_get_callback_args(menu, menu_get_position(menu));
                            break;
                        case RP2040_INPUT_BUTTON_MENU:
                            {
                                launcher_app_t* app = (launcher_app_t*) menu_get_callback_args(menu, menu_get_position(menu));
                                if (app != NULL) {
                                    if (show_app_details(button_queue, pax_buffer, ili9341, app)) {
                                        reload = true;
                                        quit   = true;
                                    } else {
                                        render = true;
                                    }
                                }
                                break;
                            }
                        case RP2040_INPUT_BUTTON_SELECT:
                            break;
                        default:
                            break;
                    }
                }
            }

            if (app_to_start != NULL) {
                start_app(button_queue, pax_buffer, ili9341, app_to_start);
                app_to_start = NULL;
                render       = true;
            }
        }

        for (size_t index = 0; index < menu_get_length(menu); index++) {
            free_launcher_app(menu_get_callback_args(menu, index));
        }

        menu_free(menu);
        pax_buf_destroy(&menu_icon);
    }
    size_t ram_after = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    printf("Leak in launcher: %d (%u to %u)\r\n", ram_before - ram_after, ram_before, ram_after);
}
