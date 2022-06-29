#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "appfs.h"
#include "appfs_wrapper.h"
#include "bootscreen.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "fpga_download.h"
#include "fpga_util.h"
#include "hardware.h"
#include "ice40.h"
#include "ili9341.h"
#include "menu.h"
#include "pax_gfx.h"
#include "rp2040.h"
#include "system_wrapper.h"

static const char* TAG = "file browser";

void list_files_in_folder(const char* path) {
    DIR* dir = opendir(path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory %s", path);
        return;
    }

    struct dirent* ent;
    char           type;
    char           size[12];
    char           tpath[255];
    char           tbuffer[80];
    struct stat    sb;
    struct tm*     tm_info;
    char*          lpath = NULL;
    int            statok;

    uint64_t total  = 0;
    int      nfiles = 0;
    printf("T  Size      Date/Time         Name\n");
    printf("-----------------------------------\n");
    while ((ent = readdir(dir)) != NULL) {
        sprintf(tpath, path);
        if (path[strlen(path) - 1] != '/') {
            strcat(tpath, "/");
        }
        strcat(tpath, ent->d_name);
        tbuffer[0] = '\0';

        // Get file stat
        statok = stat(tpath, &sb);

        if (statok == 0) {
            tm_info = localtime(&sb.st_mtime);
            strftime(tbuffer, 80, "%d/%m/%Y %R", tm_info);
        } else {
            sprintf(tbuffer, "                ");
        }

        if (ent->d_type == DT_REG) {
            type = 'f';
            nfiles++;
            if (statok) {
                strcpy(size, "       ?");
            } else {
                total += sb.st_size;
                if (sb.st_size < (1024 * 1024))
                    sprintf(size, "%8d", (int) sb.st_size);
                else if ((sb.st_size / 1024) < (1024 * 1024))
                    sprintf(size, "%6dKB", (int) (sb.st_size / 1024));
                else
                    sprintf(size, "%6dMB", (int) (sb.st_size / (1024 * 1024)));
            }
        } else {
            type = 'd';
            strcpy(size, "       -");
        }

        printf("%c  %s  %s  %s\r\n", type, size, tbuffer, ent->d_name);
    }

    printf("-----------------------------------\n");
    if (total < (1024 * 1024))
        printf("   %8d", (int) total);
    else if ((total / 1024) < (1024 * 1024))
        printf("   %6dKB", (int) (total / 1024));
    else
        printf("   %6dMB", (int) (total / (1024 * 1024)));
    printf(" in %d file(s)\n", nfiles);
    printf("-----------------------------------\n");

    closedir(dir);
    free(lpath);
}

typedef struct _file_browser_menu_args {
    char type;
    char path[512];
    char label[512];
} file_browser_menu_args_t;

void find_parent_dir(char* path, char* parent) {
    size_t last_separator = 0;
    for (size_t index = 0; index < strlen(path); index++) {
        if (path[index] == '/') last_separator = index;
    }

    strcpy(parent, path);
    parent[last_separator] = '\0';
}

static bool is_esp32_binary(FILE* fd) {
    if (get_file_size(fd) < 1) return false;
    fseek(fd, 0, SEEK_SET);
    uint8_t magic_value = 0;
    fread(&magic_value, 1, 1, fd);
    fseek(fd, 0, SEEK_SET);
    return (magic_value == 0xE9);
}

static bool is_bitstream(FILE* fd) {
    const uint8_t expected_value[] = {0xFf, 0x00, 0x00, 0xff, 0x7e, 0xaa, 0x99, 0x7e};
    if (get_file_size(fd) < sizeof(expected_value)) return false;
    fseek(fd, 0, SEEK_SET);
    uint8_t file_contents[sizeof(expected_value)];
    fread(file_contents, sizeof(expected_value), 1, fd);
    fseek(fd, 0, SEEK_SET);
    return (memcmp(expected_value, file_contents, sizeof(expected_value)) == 0);
}

static void file_browser_open_file(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, const char* filename, const char* label) {
    const pax_font_t* font = pax_get_font("saira regular");
    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0xFFFFFF);

    char* path = strdup(filename);
    if (path == NULL) return;
    for (size_t position = strlen(path) - 1; position >= 0; position--) {
        if (path[position] == '/') {
            path[position] = '\0';
            break;
        }
    }

    FILE* fd = fopen(filename, "rb");
    if (fd == NULL) {
        pax_draw_text(pax_buffer, 0xFFFF0000, font, 18, 0, 0, "Failed to open file\n\nPress A or B to go back");
        ili9341_write(ili9341, pax_buffer->buf);
        ESP_LOGE(TAG, "Failed to open file");
        wait_for_button(buttonQueue);
        free(path);
        return;
    }

    if (is_esp32_binary(fd)) {
        size_t file_size = get_file_size(fd);
        fclose(fd);
        size_t appfs_free = appfsGetFreeMem();
        if (file_size <= appfs_free) {
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 0, "ESP32 application\n\nPress A to install\nPress B to go back");
            ili9341_write(ili9341, pax_buffer->buf);
            if (wait_for_button(buttonQueue)) {
                appfs_store_app(pax_buffer, ili9341, filename, label, label, 0xFFFF);
            }
        } else {
            char buffer[128];
            snprintf(buffer, sizeof(buffer),
                     "ESP32 application\nSize: %u KB\nFree: %u KB\nNot enough free space\nplease free up space\n\nPress A or B to go back", file_size / 1024,
                     appfs_free / 1024);
            pax_draw_text(pax_buffer, 0xFFFF0000, font, 18, 0, 0, buffer);
            ili9341_write(ili9341, pax_buffer->buf);
            wait_for_button(buttonQueue);
        }
        free(path);
        return;
    } else if (is_bitstream(fd)) {
        pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 0, "FPGA bitstream\n\nPress A to run\nPress B to go back");
        ili9341_write(ili9341, pax_buffer->buf);
        if (wait_for_button(buttonQueue)) {
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
                fpga_host(buttonQueue, ice40, pax_buffer, ili9341, false, path);
                fpga_irq_cleanup(ice40);
                ice40_disable(ice40);
                ili9341_init(ili9341);
            } else {
                ice40_disable(ice40);
                ili9341_init(ili9341);
                pax_background(pax_buffer, 0xFFFFFF);
                pax_draw_text(pax_buffer, 0xFFFF0000, font, 18, 0, 0, "Failed to load bitstream\n\nPress A or B to go back");
                ili9341_write(ili9341, pax_buffer->buf);
                wait_for_button(buttonQueue);
            }
        } else {
            fclose(fd);
        }
    } else {
        fclose(fd);
        pax_draw_text(pax_buffer, 0xFFFF0000, font, 18, 0, 0, "Unsupported file type\n\nPress A or B to go back");
        ili9341_write(ili9341, pax_buffer->buf);
        ESP_LOGE(TAG, "Failed to open file");
        wait_for_button(buttonQueue);
    }
    free(path);
    return;
}

void file_browser(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, const char* initial_path) {
    display_boot_screen(pax_buffer, ili9341, "Please wait...");
    char path[512] = {0};
    strncpy(path, initial_path, sizeof(path));
    while (true) {
        menu_t* menu = menu_alloc(path, 20, 18);
        DIR*    dir  = opendir(path);
        if (dir == NULL) {
            if (path[0] != 0) {
                ESP_LOGE(TAG, "Failed to open directory %s", path);
                display_boot_screen(pax_buffer, ili9341, "Failed to open directory");
                vTaskDelay(200 / portTICK_PERIOD_MS);
            }
            return;
        }
        struct dirent*            ent;
        file_browser_menu_args_t* pd_args = malloc(sizeof(file_browser_menu_args_t));
        pd_args->type                     = 'd';
        find_parent_dir(path, pd_args->path);
        menu_insert_item(menu, "../", NULL, pd_args, -1);

        while ((ent = readdir(dir)) != NULL) {
            file_browser_menu_args_t* args = malloc(sizeof(file_browser_menu_args_t));
            sprintf(args->path, path);
            if (path[strlen(path) - 1] != '/') {
                strcat(args->path, "/");
            }
            strcat(args->path, ent->d_name);

            if (ent->d_type == DT_REG) {
                args->type = 'f';
            } else {
                args->type = 'd';
            }

            snprintf(args->label, sizeof(args->label), "%s%s", ent->d_name, (args->type == 'd') ? "/" : "");
            menu_insert_item(menu, args->label, NULL, args, -1);
        }
        closedir(dir);

        bool                      render   = true;
        bool                      renderbg = true;
        bool                      exit     = false;
        file_browser_menu_args_t* menuArgs = NULL;

        while (1) {
            rp2040_input_message_t buttonMessage = {0};
            if (xQueueReceive(buttonQueue, &buttonMessage, 16 / portTICK_PERIOD_MS) == pdTRUE) {
                uint8_t pin   = buttonMessage.input;
                bool    value = buttonMessage.state;
                switch (pin) {
                    case RP2040_INPUT_JOYSTICK_DOWN:
                        if (value) {
                            menu_navigate_next(menu);
                            render = true;
                        }
                        break;
                    case RP2040_INPUT_JOYSTICK_UP:
                        if (value) {
                            menu_navigate_previous(menu);
                            render = true;
                        }
                        break;
                    case RP2040_INPUT_BUTTON_BACK:
                        if (value) {
                            menuArgs = pd_args;
                        }
                        break;
                    case RP2040_INPUT_BUTTON_ACCEPT:
                    case RP2040_INPUT_JOYSTICK_PRESS:
                        if (value) {
                            menuArgs = menu_get_callback_args(menu, menu_get_position(menu));
                        }
                        break;
                    case RP2040_INPUT_BUTTON_HOME:
                        if (value) exit = true;
                        break;
                    default:
                        break;
                }
            }

            if (renderbg) {
                pax_background(pax_buffer, 0xFFFFFF);
                pax_noclip(pax_buffer);
                const pax_font_t* font = pax_get_font("saira regular");
                pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 19, "🅰 install  🅱 back");
                renderbg = false;
            }

            if (render) {
                menu_render(pax_buffer, menu, 0, 0, 320, 220);
                ili9341_write(ili9341, pax_buffer->buf);
                render = false;
            }

            if (menuArgs != NULL) {
                if (menuArgs->type == 'd') {
                    strcpy(path, menuArgs->path);
                    break;
                } else {
                    printf("File selected: %s\n", menuArgs->path);
                    file_browser_open_file(buttonQueue, pax_buffer, ili9341, menuArgs->path, menuArgs->label);
                    // appfs_store_app(pax_buffer, ili9341, menuArgs->path, menuArgs->label, menuArgs->label, 0xFFFF);
                }
                menuArgs = NULL;
                render   = true;
                renderbg = true;
            }

            if (exit) {
                break;
            }
        }

        for (size_t index = 0; index < menu_get_length(menu); index++) {
            free(menu_get_callback_args(menu, index));
        }

        menu_free(menu);
    }
}
