#include "webusb.h"

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
#include "driver/uart.h"
#include "driver_fsoverbus.h"
#include "esp32/rom/crc.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "graphics_wrapper.h"
#include "hardware.h"
#include "ice40.h"
#include "managed_i2c.h"
#include "pax_gfx.h"
#include "system_wrapper.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#define LOG_LINES 9

char* log_lines[LOG_LINES] = {NULL};
static QueueHandle_t status_queue = NULL;

void webusb_print_status(pax_buf_t* pax_buffer, ILI9341* ili9341) {
    const pax_font_t* font = pax_font_saira_regular;
    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0x000000);
    pax_draw_text(pax_buffer, 0xFFFFFF00, font, 20, 0, 23 * 0, "WebUSB");
    for (uint8_t i = 0; i < LOG_LINES; i++) {
        if (log_lines[i] != NULL) {
            pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 20, 0, 23 * (i + 1), log_lines[i]);
        }
    }
    ili9341_write(ili9341, pax_buffer->buf);
}

void webusb_push_status(char* buffer) {
    if (log_lines[0] != NULL) free(log_lines[0]);
    for (uint8_t i = 0; i < LOG_LINES - 1; i++) {
        log_lines[i] = log_lines[i + 1];
    }
    log_lines[LOG_LINES - 1] = buffer;
}

void webusb_print_status_wrapped(char* buffer) {
    xQueueSend(status_queue, &buffer, portMAX_DELAY);
}

void webusb_main(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    status_queue = xQueueCreate(4, sizeof(char*));
    if (status_queue == NULL) return;
    driver_fsoverbus_init(&webusb_print_status_wrapped);
    webusb_enable_uart();
    while (true) {
        char* buffer = NULL;
        if (xQueueReceive(status_queue, &buffer, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (buffer != NULL) {
                webusb_push_status(buffer);
                webusb_print_status(pax_buffer, ili9341);
            }
        }
    }
}

void webusb_enable_uart() {
    fflush(stdout);
    uart_set_pin(0, -1, -1, -1, -1);
    uart_set_pin(CONFIG_DRIVER_FSOVERBUS_UART_NUM, 1, 3, -1, -1);
}

void webusb_disable_uart() {
    uart_set_pin(0, 1, 3, -1, -1);
    uart_set_pin(CONFIG_DRIVER_FSOVERBUS_UART_NUM, -1, -1, -1, -1);
}
