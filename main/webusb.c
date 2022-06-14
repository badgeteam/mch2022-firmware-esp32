#include <stdio.h>
#include <string.h>
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_system.h>
#include <esp_err.h>
#include <esp_log.h>
#include "driver/uart.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "hardware.h"
#include "managed_i2c.h"
#include "pax_gfx.h"
#include "ice40.h"
#include "system_wrapper.h"
#include "graphics_wrapper.h"
#include "esp32/rom/crc.h"
#include "appfs.h"
#include "appfs_wrapper.h"
#include "webusb.h"
#include "driver_fsoverbus.h"

void webusb_print_status(pax_buf_t* pax_buffer, ILI9341* ili9341, char* message) {
    const pax_font_t *font = pax_get_font("saira regular");
    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0x000000);
    pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 20, 0, 23*0, "WebUSB");
    pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 20, 0, 23*1, message);
    ili9341_write(ili9341, pax_buffer->buf);
}

void webusb_main(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    driver_fsoverbus_init();   
    webusb_enable_uart();
    while(true) {
        vTaskDelay(100);
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
