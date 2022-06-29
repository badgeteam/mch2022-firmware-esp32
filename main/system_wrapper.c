#include "system_wrapper.h"

#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "rp2040.h"

void restart() {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    fflush(stdout);
    esp_restart();
}

bool wait_for_button(xQueueHandle buttonQueue) {
    while (1) {
        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(buttonQueue, &buttonMessage, portMAX_DELAY) == pdTRUE) {
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
