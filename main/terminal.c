#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp32/rom/crc.h"
#include "graphics_wrapper.h"
#include "hardware.h"
#include "pax_gfx.h"
#include "system_wrapper.h"

#define LOG_LINES 12

static QueueHandle_t log_queue = NULL;

void terminal_log(char* fmt, ...) {
    char* buffer = malloc(256);
    if (buffer == NULL) return;
    buffer[255] = '\0';
    va_list va;
    va_start(va, fmt);
    vsnprintf(buffer, 255, fmt, va);
    va_end(va);
    xQueueSend(log_queue, &buffer, portMAX_DELAY);
}

void terminal_log_wrapped(char* buffer) { xQueueSend(log_queue, &buffer, portMAX_DELAY); }

static void log_event_task(void* pvParameters) {
    char* lines[LOG_LINES] = {NULL};
    pax_buf_t* pax_buffer = get_pax_buffer();
    xQueueHandle queue = (QueueHandle_t) pvParameters;
    pax_noclip(pax_buffer);
    for (;;) {
        char* buffer = NULL;
        if (xQueueReceive(queue, &buffer, portMAX_DELAY) == pdTRUE) {
            if (buffer != NULL) {
                if (lines[0] != NULL) {
                    free(lines[0]);
                    lines[0] = NULL;
                }
                for (uint8_t i = 0; i < LOG_LINES - 1; i++) {
                    lines[i] = lines[i + 1];
                }
                lines[LOG_LINES - 1] = buffer;
            }
            const pax_font_t* font = pax_font_sky_mono;
            pax_background(pax_buffer, 0xFFFFFF);
            for (uint8_t i = 0; i < LOG_LINES; i++) {
                if (lines[i] != NULL) {
                    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * i, lines[i]);
                }
            }
            display_flush();
        }
    }
    for (int i = 0; i < LOG_LINES; i++) {
        if (lines[i] != NULL) {
            free(lines[i]);
        }
    }
    vTaskDelete(NULL);
}

void terminal_start() {
    log_queue = xQueueCreate(8, sizeof(char*));
    xTaskCreate(log_event_task, "log_event_task", 2048, (void*) log_queue, 12, NULL);
}
