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
#include "hardware.h"
#include "managed_i2c.h"
#include "pax_gfx.h"
#include "ice40.h"
#include "system_wrapper.h"
#include "graphics_wrapper.h"
#include "esp32/rom/crc.h"

void webusb_install_uart() {
    fflush(stdout);
    ESP_ERROR_CHECK(uart_driver_install(0, 2048, 0, 0, NULL, 0));
    uart_config_t uart_config = {
        .baud_rate  = 921600,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    ESP_ERROR_CHECK(uart_param_config(0, &uart_config));
}

void webusb_uninstall_uart() {
    uart_driver_delete(0);
}

bool webusb_read_stdin(uint8_t* buffer, uint32_t len, uint32_t timeout) {
    int read = uart_read_bytes(0, buffer, len, timeout / portTICK_PERIOD_MS);
    return (read == len);
}

bool webusb_uart_sync(uint32_t* length, uint32_t* crc) {
    uint8_t rx_buffer[4*3];
    webusb_read_stdin(rx_buffer, sizeof(rx_buffer), 100);
    if (memcmp(rx_buffer, "WUSB", 4) != 0) return false;
    memcpy((uint8_t*) length, &rx_buffer[4 * 1], 4);
    memcpy((uint8_t*) crc, &rx_buffer[4 * 2], 4);
    return true;
}

bool webusb_uart_load(uint8_t* buffer, uint32_t length) {
    return webusb_read_stdin(buffer, length, 3000);
}

void webusb_uart_mess(const char *mess) {
    uart_write_bytes(0, mess, strlen(mess));
}

void webusb_print_status(pax_buf_t* pax_buffer, ILI9341* ili9341, char* message) {
    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0x325aa8);
    pax_draw_text(pax_buffer, 0xFFFFFFFF, NULL, 18, 0, 20*0, "WebUSB mode");
    pax_draw_text(pax_buffer, 0xFFFFFFFF, NULL, 18, 0, 20*1, message);
    ili9341_write(ili9341, pax_buffer->buf);
}

void webusb_main(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    webusb_install_uart();

    while (true) {
        webusb_print_status(pax_buffer, ili9341, "Waiting...");

        // 1) Wait for WUSB followed by data length as uint32 and CRC32 of the data as uint32
        uint32_t length, crc;
        while (!webusb_uart_sync(&length, &crc)) {
            webusb_uart_mess("WUSB");
        }

        webusb_print_status(pax_buffer, ili9341, "Receiving...");

        // 2) Allocate RAM for the data to be received
        uint8_t* buffer = malloc(length);
        if (buffer == NULL) {
            webusb_uart_mess("EMEM");
            webusb_print_status(pax_buffer, ili9341, "Error: malloc failed");
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        // 3) Receive data into the buffer
        if (!webusb_uart_load(buffer, length)) {
            free(buffer);
            webusb_uart_mess("ERCV");
            webusb_print_status(pax_buffer, ili9341, "Error: receive failed");
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        // 4) Check CRC
        uint32_t checkCrc = crc32_le(0, buffer, length);
        
        if (checkCrc != crc) {
            free(buffer);
            webusb_uart_mess("ECRC");
            webusb_print_status(pax_buffer, ili9341, "Error: CRC invalid");
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        webusb_uart_mess("OKOK");
        webusb_print_status(pax_buffer, ili9341, "Packet received");

        // To-do: parse packet
    }
    
    webusb_uninstall_uart();
}
