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

#define LOG_LINES 24

typedef struct terminal_line {
    char* data;
    bool is_dynamically_allocated;
} terminal_line_t;

static QueueHandle_t log_queue = NULL;

void terminal_printf(char* fmt, ...) {
    terminal_line_t line;
    line.data = malloc(256);
    line.is_dynamically_allocated = true;
    if (line.data == NULL) return;
    line.data[255] = '\0';
    va_list va;
    va_start(va, fmt);
    vsnprintf(line.data, 255, fmt, va);
    va_end(va);

    xQueueSend(log_queue, &line, portMAX_DELAY);
}

void terminal_log(char* buffer) {
    terminal_line_t line;
    line.data = buffer;
    line.is_dynamically_allocated = false;
    xQueueSend(log_queue, &line, portMAX_DELAY);
}

static void log_event_task(void* pvParameters) {
    const pax_font_t* font = pax_font_sky_mono;
    terminal_line_t lines[LOG_LINES] = {0};
    int offset = 0;
    pax_buf_t* pax_buffer = get_pax_buffer();
    xQueueHandle queue = (QueueHandle_t) pvParameters;
    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0x0000FF); // Blue screen
    display_flush();
    for (;;) {
        if (lines[offset].data != NULL && lines[offset].is_dynamically_allocated) {
            free(lines[offset].data);
        }
        if (xQueueReceive(queue, &lines[offset], portMAX_DELAY) != pdTRUE) {
            break;
        }
        offset = (offset + 1) % LOG_LINES;
        pax_background(pax_buffer, 0x000000);
        int position = 0;
        for (int index = 0; index < LOG_LINES; index++) {
            terminal_line_t* line = &lines[(offset + index) % LOG_LINES];
            if (line->data != NULL) {
                pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 9, 0, 10 * position, line->data);
                position++;
            }
        }
        display_flush();
    }
    for (int index = 0; index < LOG_LINES; index++) {
        if (lines[index].data != NULL && lines[index].is_dynamically_allocated) {
            free(lines[index].data);
        }
    }
    pax_background(pax_buffer, 0xFF0000); // Red screen
    display_flush();
    vTaskDelete(NULL);
}

void terminal_start() {
    log_queue = xQueueCreate(8, sizeof(terminal_line_t));
    xTaskCreate(log_event_task, "log_event_task", 2048, (void*) log_queue, 12, NULL);
}
