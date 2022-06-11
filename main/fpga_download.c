#include <stdarg.h>
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

static void fpga_install_uart() {
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

static void fpga_uninstall_uart() {
    uart_driver_delete(0);
}

static bool fpga_read_stdin(uint8_t* buffer, uint32_t len, uint32_t timeout) {
    int read = uart_read_bytes(0, buffer, len, timeout / portTICK_PERIOD_MS);
    return (read == len);
}

static bool fpga_uart_sync(uint32_t* length, uint32_t* crc) {
    uint8_t data[256];
    uart_read_bytes(0, data, sizeof(data), 10 / portTICK_PERIOD_MS);
    char command[] = "FPGA";
    uart_write_bytes(0, command, 4);
    uint8_t rx_buffer[4 * 3];
    fpga_read_stdin(rx_buffer, sizeof(rx_buffer), 1000);
    if (memcmp(rx_buffer, "FPGA", 4) != 0) return false;
    memcpy((uint8_t*) length, &rx_buffer[4 * 1], 4);
    memcpy((uint8_t*) crc, &rx_buffer[4 * 2], 4);
    return true;
}

static bool fpga_uart_load(uint8_t* buffer, uint32_t length) {
    return fpga_read_stdin(buffer, length, 3000);
}

static void fpga_uart_mess(const char *mess) {
    uart_write_bytes(0, mess, strlen(mess));
}

static void fpga_display_message(
        pax_buf_t* pax_buffer, ILI9341* ili9341,
        uint32_t bg, uint32_t fg,
        const char *fmt, ...
    )
{
    char message[256];
    va_list va;
    char *c, *m;
    int line;
    bool done;

    // Print message in internal buffer
    va_start(va, fmt);
    vsnprintf(message, sizeof(message), fmt, va);
    va_end(va);

    // Clear screen
    pax_noclip(pax_buffer);
    pax_background(pax_buffer, bg);

    // Scan
    done = false;
    line = 0;
    m = c = message;

    while (!done) {
        // End ?
        done = (*c == '\0');

        // Print ?
        if (*c == '\0' || *c == '\n') {
            *c = '\0';
            pax_draw_text(pax_buffer, fg, NULL, 18, 0, 20*line, m);
            m = c + 1;
            line++;
        }

        // Next char
        c++;
    }

    // Send to screen
    ili9341_write(ili9341, pax_buffer->buf);
}

static esp_err_t fpga_process_events(xQueueHandle buttonQueue, ICE40* ice40, uint16_t *key_state, uint16_t *idle_count)
{
    rp2040_input_message_t buttonMessage = {0};
    while (xQueueReceive(buttonQueue, &buttonMessage, 0) == pdTRUE) {
        uint8_t pin = buttonMessage.input;
        bool value = buttonMessage.state;
        uint16_t key_mask = 0;
        switch(pin) {
            case RP2040_INPUT_JOYSTICK_DOWN:
                key_mask = 1 << 0;
                break;
            case RP2040_INPUT_JOYSTICK_UP:
                key_mask = 1 << 1;
                break;
            case RP2040_INPUT_JOYSTICK_LEFT:
                key_mask = 1 << 2;
                break;
            case RP2040_INPUT_JOYSTICK_RIGHT:
                key_mask = 1 << 3;
                break;
            case RP2040_INPUT_JOYSTICK_PRESS:
                key_mask = 1 << 4;
                break;
            case RP2040_INPUT_BUTTON_HOME:
                key_mask = 1 << 5;
                break;
            case RP2040_INPUT_BUTTON_MENU:
                key_mask = 1 << 6;
                break;
            case RP2040_INPUT_BUTTON_SELECT:
                key_mask = 1 << 7;
                break;
            case RP2040_INPUT_BUTTON_START:
                key_mask = 1 << 8;
                break;
            case RP2040_INPUT_BUTTON_ACCEPT:
                key_mask = 1 << 9;
                break;
            case RP2040_INPUT_BUTTON_BACK:
                key_mask = 1 << 10;
            default:
                break;
        }
        if (key_mask != 0)
        {
            if (value) {
                *key_state |= key_mask;
            }
            else {
                *key_state &= ~key_mask;
            }

            uint8_t spi_message[5] = { 0xf4 };
            spi_message[1] = *key_state >> 8;
            spi_message[2] = *key_state & 0xff;
            spi_message[3] = key_mask >> 8;
            spi_message[4] = key_mask & 0xff;
            esp_err_t res = ice40_send(ice40, spi_message, 5);
            if (res != ESP_OK) {
                return res;
            }
        }
        *idle_count = 0;
    }
    return ESP_OK;
}

void fpga_download(xQueueHandle buttonQueue, ICE40* ice40, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    char message[64];

    fpga_display_message(pax_buffer, ili9341, 0x325aa8, 0xFFFFFFFF,
        "FPGA download mode\nPreparing...");

    fpga_install_uart();

    ice40_disable(ice40);
    ili9341_init(ili9341);

    uint8_t counter = 0;
    uint32_t length = 0;
    uint32_t crc = 0;
    while (!fpga_uart_sync(&length, &crc)) {
        const char *dots[] = { "", ".", "..", "..." };
        fpga_display_message(pax_buffer, ili9341, 0x325aa8, 0xFFFFFFFF,
            "FPGA download mode\nWaiting for bitstream%s", dots[counter]);
        counter = (counter + 1) & 3;
    }

    while (true) {
        fpga_display_message(pax_buffer, ili9341, 0x325aa8, 0xFFFFFFFF,
            "FPGA download mode\nReceiving bitstream...");

        uint8_t* buffer = malloc(length);
        if (buffer == NULL) {
            fpga_display_message(pax_buffer, ili9341, 0xa85a32, 0xFFFFFFFF,
                "FPGA download mode\nMalloc failed");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            fpga_uninstall_uart();
            return;
        }
        if (!fpga_uart_load(buffer, length)) {
            free(buffer);
            fpga_display_message(pax_buffer, ili9341, 0xa85a32, 0xFFFFFFFF,
                "FPGA download mode\nTimeout while loading");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            fpga_uninstall_uart();
            return;
        }

        uint32_t checkCrc = crc32_le(0, buffer, length);

        if (checkCrc != crc) {
            free(buffer);
            fpga_display_message(pax_buffer, ili9341, 0xa85a32, 0xFFFFFFFF,
                "FPGA download mode\nCRC incorrect\nProvided CRC:   %08X\nCalculated CRC: %08X",
                crc, checkCrc);
            snprintf(message, sizeof(message), "CRC incorrect %08X %08x\n", crc, checkCrc);
            fpga_uart_mess(message);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            fpga_uninstall_uart();
            return;
        }
        fpga_uart_mess("CRC correct\n");

        ili9341_deinit(ili9341);
        ili9341_select(ili9341, false);
        vTaskDelay(200 / portTICK_PERIOD_MS);
        ili9341_select(ili9341, true);

        esp_err_t res = ice40_load_bitstream(ice40, buffer, length);
        free(buffer);

        if (res != ESP_OK) {
            ice40_disable(ice40);
            ili9341_init(ili9341);
            fpga_display_message(pax_buffer, ili9341, 0xa85a32, 0xFFFFFFFF,
                "FPGA download mode\nUpload failed: %d", res);
            snprintf(message, sizeof(message), "uploading bitstream failed with %d\n", res);
            fpga_uart_mess(message);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            fpga_uninstall_uart();
            return;
        }
        snprintf(message, sizeof(message), "bitstream has uploaded\n");
        fpga_uart_mess(message);

        // Waiting for next download and sending key strokes to FPGA
        uint16_t key_state = 0;
        uint16_t idle_count = 0;
        while (true) {
            if (idle_count >= 200) {
                if (fpga_uart_sync(&length, &crc)) {
                    break;
                }
                idle_count = 0;
            }
            esp_err_t res = fpga_process_events(buttonQueue, ice40, &key_state, &idle_count);
            if (res != ESP_OK) {
                ice40_disable(ice40);
                ili9341_init(ili9341);
                fpga_display_message(pax_buffer, ili9341, 0xa85a32, 0xFFFFFFFF,
                    "FPGA download mode\nError: %d", res);
                snprintf(message, sizeof(message), "processing events failed with %d\n", res);
                fpga_uart_mess(message);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                fpga_uninstall_uart();
                return;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
            idle_count++;
        }
        ice40_disable(ice40);
        ili9341_init(ili9341);
    }
}
