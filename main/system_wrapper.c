#include "system_wrapper.h"

#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "hardware.h"
#include "rp2040.h"

void restart() {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    fflush(stdout);
    esp_restart();
}

bool wait_for_button() {
    RP2040* rp2040 = get_rp2040();
    if (rp2040 == NULL) return false;
    while (1) {
        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(rp2040->queue, &buttonMessage, portMAX_DELAY) == pdTRUE) {
            if (buttonMessage.state) {
                switch (buttonMessage.input) {
                    case RP2040_INPUT_BUTTON_BACK:
                    case RP2040_INPUT_BUTTON_HOME:
                        return false;
                    case RP2040_INPUT_BUTTON_ACCEPT:
                        return true;
                    default:
                        break;
                }
            }
        }
    }
}

uint8_t* load_file_to_ram(FILE* fd) {
    fseek(fd, 0, SEEK_END);
    size_t fsize = ftell(fd);
    fseek(fd, 0, SEEK_SET);
    uint8_t* file = malloc(fsize);
    if (file == NULL) return NULL;
    fread(file, fsize, 1, fd);
    return file;
}

size_t get_file_size(FILE* fd) {
    fseek(fd, 0, SEEK_END);
    size_t fsize = ftell(fd);
    fseek(fd, 0, SEEK_SET);
    return fsize;
}

bool remove_recursive(const char* path) {
    size_t         path_len;
    char*          full_path;
    DIR*           dir;
    struct stat    stat_path, stat_entry;
    struct dirent* entry;

    // stat for the path
    stat(path, &stat_path);

    // if path does not exists or is not dir - exit with status -1
    if (S_ISDIR(stat_path.st_mode) == 0) {
        // printf("%s: %s\n", "Is not directory", path);
        if (unlink(path) == 0) {
            // printf("Removed a file: %s\n", full_path);
            return true;
        } else {
            // printf("Can`t remove a file: %s\n", full_path);
            return false;
        }
    }

    // if not possible to read the directory for this user
    if ((dir = opendir(path)) == NULL) {
        // printf("%s: %s\n", "Can`t open directory", path);
        return false;
    }

    bool failed = false;

    // the length of the path
    path_len = strlen(path);

    // iteration through entries in the directory
    while ((entry = readdir(dir)) != NULL) {
        // skip entries "." and ".."
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }

        // determinate a full path of an entry
        full_path = calloc(path_len + strlen(entry->d_name) + 2, sizeof(char));
        strcpy(full_path, path);
        strcat(full_path, "/");
        strcat(full_path, entry->d_name);

        // stat for the entry
        stat(full_path, &stat_entry);

        // recursively remove a nested directory
        if (S_ISDIR(stat_entry.st_mode) != 0) {
            remove_recursive(full_path);
            continue;
        }

        // remove a file object
        if (unlink(full_path) == 0) {
            // printf("Removed a file: %s\n", full_path);
        } else {
            // printf("Can`t remove a file: %s\n", full_path);
            failed = true;
        }
        free(full_path);
    }

    // remove the devastated directory and close the object of it
    if (rmdir(path) == 0) {
        // printf("Removed a directory: %s\n", path);
    } else {
        // printf("Can`t remove a directory: %s\n", path);
        failed = true;
    }
    closedir(dir);
    return !failed;
}
