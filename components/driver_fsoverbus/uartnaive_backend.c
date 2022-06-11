#include "include/fsob_backend.h"
#include "include/driver_fsoverbus.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/uart.h>

#define TAG "fsob_nuart"

#if (CONFIG_DRIVER_FSOVERBUS_BACKEND == 2)

bool fsob_uart_sync(uint32_t* size, uint16_t* command, uint32_t* message_id) {
    uint16_t verif = 0; //Verif field
    uint8_t rx_buffer[12];
    int read = uart_read_bytes(CONFIG_DRIVER_FSOVERBUS_UART_NUM, rx_buffer, sizeof(rx_buffer), pdMS_TO_TICKS(1000));
    if (read != sizeof(rx_buffer)) return false;
    verif = *((uint16_t *) &rx_buffer[6]);
    if (verif != 0xADDE) return false;
    *command = *((uint16_t *) &rx_buffer[0]);
    *size = *((uint32_t *) &rx_buffer[2]);
    *message_id = *((uint32_t *) &rx_buffer[8]);
    return true;
}

void fsob_task(void *pvParameter) {
    while (true) {
        //webusb_print_status(pax_buffer, ili9341, "Waiting...");

        // 1) Wait for WUSB followed by data length as uint32 and CRC32 of the data as uint32
        uint32_t size, message_id;
        uint16_t command;
        while (!fsob_uart_sync(&size, &command, &message_id)) {
            vTaskDelay(10);
        }

        //webusb_print_status(pax_buffer, ili9341, "Receiving...");

        // 2) Allocate RAM for the data to be received
        uint8_t* buffer = NULL;
        if (size > 0) {
            buffer = malloc(size);
            if (buffer == NULL) {
                //webusb_uart_mess("EMEM");
                //webusb_print_status(pax_buffer, ili9341, "Error: malloc failed");
                //vTaskDelay(100 / portTICK_PERIOD_MS);
                continue;
            }

            // 3) Receive data into the buffer
            int read = uart_read_bytes(CONFIG_DRIVER_FSOVERBUS_UART_NUM, buffer, size, pdMS_TO_TICKS(50));
            if (read != size) {
                free(buffer);
                //webusb_uart_mess("ERCV");
                char outputstring[100] = {0};
                snprintf(outputstring, 100, "Error: rec %d %d", read, size);
                //webusb_print_status(pax_buffer, ili9341, outputstring);
                //vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }

        //webusb_print_status(pax_buffer, ili9341, "Packet received");
        handleFSCommand(buffer, command, message_id, size, size, size);
        if(buffer != NULL) {
            free(buffer);
        }
        //webusb_print_status(pax_buffer, ili9341, "Done");
    }
}

void fsob_init() {
    ESP_ERROR_CHECK(uart_driver_install(CONFIG_DRIVER_FSOVERBUS_UART_NUM, 16*1024, 0, 0, NULL, 0));
    uart_config_t uart_config = {
        .baud_rate  = CONFIG_DRIVER_FSOVERBUS_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    ESP_ERROR_CHECK(uart_param_config(CONFIG_DRIVER_FSOVERBUS_UART_NUM, &uart_config));

    xTaskCreatePinnedToCore(fsob_task, "fsoverbus_uart", 16000, NULL, 100, NULL, 0);
}

void fsob_reset() {
    
}

void fsob_write_bytes(const char *src, size_t size) {
    uart_write_bytes(CONFIG_DRIVER_FSOVERBUS_UART_NUM, src, size);
}

#endif