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
#include "fpga_util.h"

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
    static int step = 0;
    static int l;
    static uint8_t data[8];
    static int64_t timeout;

    if (step && (esp_timer_get_time() > timeout))
        step = 0;

    switch (step) {
    /* Step 0: Flush pending data and send sync word */
    case 0:
        uart_flush(0);
        uart_write_bytes(0, "FPGA", 4);
        step++;
        l = 0;
        timeout = esp_timer_get_time() + 500000; /* setup 0.5s timeout */
        /* fall-through */

    /* Step 1: Receive the 'FPGA' header */
    case 1:
        l += uart_read_bytes(0, &data[l], 4-l, 0);
        if (l != 4)
            break;
        if (memcmp(data, "FPGA", 4) != 0) {
            step = 0;
            break;
        }
        step++;
        l = 0;
        /* fall-through */

    /* Step 2: Receive the length and CRC */
    case 2:
        l += uart_read_bytes(0, &data[l], 8-l, 0);
        if (l != 8)
            break;
        memcpy((uint8_t*) length, &data[0], 4);
        memcpy((uint8_t*) crc,    &data[4], 4);
        step++;
        return true;

    /* Step 3: Just wait for next attempt */
    case 3:
        break;

    /* Unknown: Reset */
    default:
        step = 0;
        break;
    }

    return false;
}

static bool fpga_uart_load(uint8_t* buffer, uint32_t length) {
    return fpga_read_stdin(buffer, length, 3000);
}

static void fpga_uart_mess(const char *fmt, ...) {
    char message[64];
    va_list va;
    int l;

    // Print message in internal buffer
    va_start(va, fmt);
    l = vsnprintf(message, sizeof(message), fmt, va);
    va_end(va);

    // Send message
    uart_write_bytes(0, message, l);
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

    const pax_font_t *font = pax_get_font("saira regular");
    while (!done) {
        // End ?
        done = (*c == '\0');

        // Print ?
        if (*c == '\0' || *c == '\n') {
            *c = '\0';
            pax_draw_text(pax_buffer, fg, font, 18, 0, 20*line, m);
            m = c + 1;
            line++;
        }

        // Next char
        c++;
    }

    // Send to screen
    ili9341_write(ili9341, pax_buffer->buf);
}


void fpga_download(xQueueHandle buttonQueue, ICE40* ice40, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    uint8_t *buffer = NULL;

    fpga_display_message(pax_buffer, ili9341, 0x325aa8, 0xFFFFFFFF,
        "FPGA download mode\nPreparing...");

    fpga_install_uart();
    fpga_btn_reset();

    ice40_disable(ice40);
    ili9341_init(ili9341);

    uint8_t  counter = 0;
    uint32_t length = 0;
    uint32_t crc = 0;

    while (!fpga_uart_sync(&length, &crc))
    {
        const char *dots[] = { "", ".", "..", "..." };
        uint8_t counter_new = (esp_timer_get_time() >> 19) & 0x3;
        if (counter != counter_new) {
            fpga_display_message(pax_buffer, ili9341, 0x325aa8, 0xFFFFFFFF,
                "FPGA download mode\nWaiting for bitstream%s", dots[counter]);
            counter = counter_new;
        } else {
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }

    while (true) {
        fpga_display_message(pax_buffer, ili9341, 0x325aa8, 0xFFFFFFFF,
            "FPGA download mode\nReceiving bitstream...");

        buffer = malloc(length);
        if (buffer == NULL) {
            fpga_display_message(pax_buffer, ili9341, 0xa85a32, 0xFFFFFFFF,
                "FPGA download mode\nMalloc failed");
            goto error;
        }
        if (!fpga_uart_load(buffer, length)) {
            fpga_display_message(pax_buffer, ili9341, 0xa85a32, 0xFFFFFFFF,
                "FPGA download mode\nTimeout while loading");
            goto error;
        }

        uint32_t checkCrc = crc32_le(0, buffer, length);

        if (checkCrc != crc) {
            fpga_display_message(pax_buffer, ili9341, 0xa85a32, 0xFFFFFFFF,
                "FPGA download mode\nCRC incorrect\nProvided CRC:   %08X\nCalculated CRC: %08X",
                crc, checkCrc);
            fpga_uart_mess("CRC incorrect %08X %08x\n", crc, checkCrc);
            goto error;
        }
        fpga_uart_mess("CRC correct\n");

        ili9341_deinit(ili9341);
        ili9341_select(ili9341, false);
        vTaskDelay(200 / portTICK_PERIOD_MS);
        ili9341_select(ili9341, true);

        esp_err_t res = ice40_load_bitstream(ice40, buffer, length);
        free(buffer);
        buffer = NULL;

        if (res != ESP_OK) {
            ice40_disable(ice40);
            ili9341_init(ili9341);
            fpga_display_message(pax_buffer, ili9341, 0xa85a32, 0xFFFFFFFF,
                "FPGA download mode\nUpload failed: %d", res);
            fpga_uart_mess("uploading bitstream failed with %d\n", res);
            goto error;
        }
        fpga_uart_mess("bitstream has uploaded\n");

        // Waiting for next download and sending key strokes to FPGA
        while (true) {
            if (fpga_uart_sync(&length, &crc)) {
                break;
            }
            esp_err_t res = fpga_btn_forward_events(ice40, buttonQueue);
            if (res != ESP_OK) {
                ice40_disable(ice40);
                ili9341_init(ili9341);
                fpga_display_message(pax_buffer, ili9341, 0xa85a32, 0xFFFFFFFF,
                    "FPGA download mode\nError: %d", res);
                fpga_uart_mess("processing events failed with %d\n", res);
                goto error;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        ice40_disable(ice40);
        ili9341_init(ili9341);
    }

    return;

error:
    free(buffer);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    fpga_uninstall_uart();
    return;
}
