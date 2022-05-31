#include <stdio.h>
#include <string.h>
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_system.h>
#include <esp_err.h>
#include <esp_log.h>
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "appfs.h"
#include "ili9341.h"
#include "pax_gfx.h"
#include "menu.h"
#include "rp2040.h"
#include "appfs_wrapper.h"

static const char *TAG = "file browser";

void list_files_in_folder(const char* path) {
    DIR* dir = opendir(path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory %s", path);
        return;
    }

    struct dirent *ent;
    char type;
    char size[12];
    char tpath[255];
    char tbuffer[80];
    struct stat sb;
    struct tm *tm_info;
    char *lpath = NULL;
    int statok;

    uint64_t total = 0;
    int nfiles = 0;
    printf("T  Size      Date/Time         Name\n");
    printf("-----------------------------------\n");
    while ((ent = readdir(dir)) != NULL) {
        sprintf(tpath, path);
        if (path[strlen(path)-1] != '/') {
            strcat(tpath,"/");
        }
        strcat(tpath,ent->d_name);
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
                if (sb.st_size < (1024*1024)) sprintf(size,"%8d", (int)sb.st_size);
                else if ((sb.st_size/1024) < (1024*1024)) sprintf(size,"%6dKB", (int)(sb.st_size / 1024));
                else sprintf(size,"%6dMB", (int)(sb.st_size / (1024 * 1024)));
            }
        } else {
            type = 'd';
            strcpy(size, "       -");
        }

        printf("%c  %s  %s  %s\r\n", type, size, tbuffer, ent->d_name);
    }

    printf("-----------------------------------\n");
    if (total < (1024*1024)) printf("   %8d", (int)total);
    else if ((total/1024) < (1024*1024)) printf("   %6dKB", (int)(total / 1024));
    else printf("   %6dMB", (int)(total / (1024 * 1024)));
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

void file_browser(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, const char* initial_path) {
    char path[512] = {0};
    strncpy(path, initial_path, sizeof(path));
    while (true) {
        menu_t* menu = menu_alloc(path);
        DIR* dir = opendir(path);
        if (dir == NULL) {
            ESP_LOGE(TAG, "Failed to open directory %s", path);
            return;
        }
        struct dirent *ent;
        file_browser_menu_args_t* pd_args = malloc(sizeof(file_browser_menu_args_t));
        pd_args->type = 'd';
        find_parent_dir(path, pd_args->path);
        printf("Parent dir: %s\n", pd_args->path);
        menu_insert_item(menu, "../", NULL, pd_args, -1);

        while ((ent = readdir(dir)) != NULL) {
            file_browser_menu_args_t* args = malloc(sizeof(file_browser_menu_args_t));
            sprintf(args->path, path);
            if (path[strlen(path)-1] != '/') {
                strcat(args->path,"/");
            }
            strcat(args->path,ent->d_name);

            if (ent->d_type == DT_REG) {
                args->type = 'f';
            } else {
                args->type = 'd';
            }

            printf("%c %s %s\r\n", args->type, ent->d_name, args->path);

            snprintf(args->label, sizeof(args->label), "%s%s", ent->d_name, (args->type == 'd') ? "/" : "");
            menu_insert_item(menu, args->label, NULL, args, -1);
        }
        closedir(dir);

        bool render = true;
        bool exit = false;
        file_browser_menu_args_t* menuArgs = NULL;
        
        pax_background(pax_buffer, 0xFFFFFF);
        pax_noclip(pax_buffer);
        pax_draw_text(pax_buffer, 0xFF000000, NULL, 18, 5, 240 - 19, "[A] install  [B] back");

        while (1) {
            rp2040_input_message_t buttonMessage = {0};
            if (xQueueReceive(buttonQueue, &buttonMessage, 16 / portTICK_PERIOD_MS) == pdTRUE) {
                uint8_t pin = buttonMessage.input;
                bool value = buttonMessage.state;
                switch(pin) {
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

            if (render) {
                menu_render(pax_buffer, menu, 0, 0, 320, 220, 0xFF72008a);
                ili9341_write(ili9341, pax_buffer->buf);
                render = false;
            }

            if (menuArgs != NULL) {
                if (menuArgs->type == 'd') {
                    strcpy(path, menuArgs->path);
                    break;
                } else {
                    printf("File selected: %s\n", menuArgs->path);
                    appfs_store_app(pax_buffer, ili9341, menuArgs->path, menuArgs->label);
                }
                menuArgs = NULL;
                render = true;
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
