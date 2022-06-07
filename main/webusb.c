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
#include "appfs.h"

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
    webusb_read_stdin(rx_buffer, sizeof(rx_buffer), 1000);
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

void webusb_handle(uint8_t* buffer, size_t buffer_length) {
    if ((buffer_length < 4) || (strnlen((char*) buffer, 4) < 4)) { // All commands use 4 bytes of ASCII
        webusb_uart_mess("ENOC");
        return;
    }

    // AppFS command: list files
    if (strncmp((char*) buffer, "APLS", 4) == 0) {
        webusb_uart_mess("APLS");

        uint32_t amount_of_files = 0;
        uint32_t buffer_size = 0;
        appfs_handle_t appfs_fd = APPFS_INVALID_FD;
        while (1) {
            appfs_fd = appfsNextEntry(appfs_fd);
            if (appfs_fd == APPFS_INVALID_FD) break;
            const char* name;
            int file_size;
            appfsEntryInfo(appfs_fd, &name, &file_size);
            amount_of_files++;
            buffer_size += sizeof(appfs_handle_t) + strlen(name);
        }
        uart_write_bytes(0, &amount_of_files, 4);
        uart_write_bytes(0, &buffer_size, 4);
        
        appfs_fd = APPFS_INVALID_FD;
        while (1) {
            appfs_fd = appfsNextEntry(appfs_fd);
            if (appfs_fd == APPFS_INVALID_FD) break;
            const char* name;
            int file_size;
            appfsEntryInfo(appfs_fd, &name, &file_size);
            uart_write_bytes(0, &file_size, 4);
            uint32_t name_length = strlen(name);
            uart_write_bytes(0, &name_length, 4);
            uart_write_bytes(0, name, name_length);
        }
        return;
    }

    // AppFS command: remove file
    if (strncmp((char*) buffer, "APRM", 4) == 0) {
        webusb_uart_mess("APRM");
        char* filename = (char*) &buffer[4];
        if (buffer[buffer_length - 1] != 0x00) {
            webusb_uart_mess("ESTR");
        } else {
            esp_err_t res = appfsDeleteFile(filename);
            if (res == ESP_OK) {
                webusb_uart_mess("OKOK");
            } else {
                webusb_uart_mess("FAIL");
            } 
        }
        return;
    }

    // AppFS command: open file (read)
    if (strncmp((char*) buffer, "APOR", 4) == 0) {
        webusb_uart_mess("APOR");
        return;
    }

    // AppFS command: open file (write)
    if (strncmp((char*) buffer, "APOW", 4) == 0) {
        webusb_uart_mess("APOW");
        return;
    }

    // AppFS command: boot file
    if (strncmp((char*) buffer, "APBT", 4) == 0) {
        webusb_uart_mess("APBT");
        return;
    }
    
    // -----
    
    // Filesystem command: list folder
    if (strncmp((char*) buffer, "FSLS", 4) == 0) {
        webusb_uart_mess("FSLS");
        return;
    }

    // Filesystem command: remove file / directory
    if (strncmp((char*) buffer, "FSRM", 4) == 0) {
        webusb_uart_mess("FSRM");
        return;
    }

    // Filesystem command: create directory
    if (strncmp((char*) buffer, "FSMD", 4) == 0) {
        webusb_uart_mess("FSMD");
        return;
    }

    // Filesystem command: open file (read)
    if (strncmp((char*) buffer, "FSOR", 4) == 0) {
        webusb_uart_mess("FSOR");
        return;
    }

    // Filesystem command: open file (write)
    if (strncmp((char*) buffer, "FSOW", 4) == 0) {
        webusb_uart_mess("FSOW");
        return;
    }

    // Filesystem command: open file (append)
    if (strncmp((char*) buffer, "FSOA", 4) == 0) {
        webusb_uart_mess("FSOA");
        return;
    }
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

        webusb_handle(buffer, length);
        
        free(buffer);
    }
    
    webusb_uninstall_uart();
}
