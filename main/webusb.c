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

#include "driver_fsoverbus.h"

void webusb_install_uart() {
    fflush(stdout);
    esp_log_level_set("*", ESP_LOG_NONE);
    ESP_ERROR_CHECK(uart_driver_install(0, 16*1024, 0, 0, NULL, 0));
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
    int read = uart_read_bytes(0, buffer, len, pdMS_TO_TICKS(timeout));
    return (read == len);
}

bool webusb_uart_sync(uint32_t* size, uint32_t* recv, uint16_t* command, uint32_t* message_id) {
    *recv = 0; //Total bytes received so far
    uint16_t verif = 0; //Verif field
    uint8_t rx_buffer[12];
    webusb_read_stdin(rx_buffer, sizeof(rx_buffer), 1000);
    verif = *((uint16_t *) &rx_buffer[6]);
    if (verif != 0xADDE) return false;
    *command = *((uint16_t *) &rx_buffer[0]);
    *size = *((uint32_t *) &rx_buffer[2]);
    *message_id = *((uint32_t *) &rx_buffer[8]);
    return true;
}

bool webusb_uart_load(uint8_t* buffer, uint32_t length) {
    return webusb_read_stdin(buffer, length, 50);
}

void webusb_uart_mess(const char *mess) {
    uart_write_bytes(0, mess, strlen(mess));
}

void webusb_print_status(pax_buf_t* pax_buffer, ILI9341* ili9341, char* message) {
    const pax_font_t *font = pax_get_font("saira regular");
    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0x000000);
    pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 20, 0, 23*0, "WebUSB");
    pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 20, 0, 23*1, message);
    ili9341_write(ili9341, pax_buffer->buf);
}

void webusb_main(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    webusb_install_uart();
    driver_fsoverbus_init();
    while (true) {
        webusb_print_status(pax_buffer, ili9341, "Waiting...");

        // 1) Wait for WUSB followed by data length as uint32 and CRC32 of the data as uint32
        uint32_t size, message_id, recv;
        uint16_t command;
        while (!webusb_uart_sync(&size, &recv, &command, &message_id)) {
            vTaskDelay(10);
        }

        webusb_print_status(pax_buffer, ili9341, "Receiving...");

        // 2) Allocate RAM for the data to be received
        uint8_t* buffer = NULL;
        if (size > 0) {
            buffer = malloc(size);
            if (buffer == NULL) {
                //webusb_uart_mess("EMEM");
                webusb_print_status(pax_buffer, ili9341, "Error: malloc failed");
                vTaskDelay(100 / portTICK_PERIOD_MS);
                continue;
            }

            // 3) Receive data into the buffer
            int read = uart_read_bytes(0, buffer, size, pdMS_TO_TICKS(50));
            if (read != size) {
                free(buffer);
                //webusb_uart_mess("ERCV");
                char outputstring[100] = {0};
                snprintf(outputstring, 100, "Error: rec %d %d", read, size);
                webusb_print_status(pax_buffer, ili9341, outputstring);
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }

        webusb_print_status(pax_buffer, ili9341, "Packet received");
        handleFSCommand(buffer, command, message_id, size, size, size);
        if(buffer != NULL) {
            free(buffer);
        }
        webusb_print_status(pax_buffer, ili9341, "Done");
    }
    
    webusb_uninstall_uart();
}

void fsob_write_bytes(uint8_t *data, int len) {
    uart_write_bytes(0, data, len);
}
