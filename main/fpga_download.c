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

static bool fpga_uart_sync(void) {
    static int step = 0;
    static int l;
    static uint8_t data[4];
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
        return true;

    /* Step 2: Just wait for next attempt */
    case 2:
        break;

    /* Unknown: Reset */
    default:
        step = 0;
        break;
    }

    return false;
}

static bool fpga_uart_load(uint8_t* buffer, uint32_t length) {
    return fpga_read_stdin(buffer, length, 1000);
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

static bool fpga_uart_download(ICE40* ice40, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    TickType_t timeout = 1000 / portTICK_PERIOD_MS;
    uint8_t *buffer = NULL;
    bool done = false;
    struct {
        uint8_t  type;
        uint32_t fid;
        uint32_t len;
        uint32_t crc;
    } __attribute__((packed)) header;

    while (!done)
    {
        // Header
        uart_read_bytes(0, &header, sizeof(header), timeout);

#if 0
        fpga_uart_mess("hdr: type=%d, fid=%08x, len=%08x, crc=%08x\n", header.type, header.fid, header.len, header.crc);
#endif

        // Payload
        if (header.len) {
            // Alloc zone to store content
            buffer = malloc(header.len);
            if (buffer == NULL) {
                fpga_display_message(pax_buffer, ili9341, 0xa85a32, 0xFFFFFFFF,
                    "FPGA download mode\nMalloc failed");
                return false;
            }

            // Read data in
            if (!fpga_uart_load(buffer, header.len)) {
                fpga_display_message(pax_buffer, ili9341, 0xa85a32, 0xFFFFFFFF,
                    "FPGA download mode\nTimeout while loading");
                return false;
            }

            // Validate CRC
            uint32_t checkCrc = crc32_le(0, buffer, header.len);
            if (checkCrc != header.crc) {
                fpga_display_message(pax_buffer, ili9341, 0xa85a32, 0xFFFFFFFF,
                    "FPGA download mode\nCRC incorrect\nProvided CRC:   %08X\nCalculated CRC: %08X",
                    header.crc, checkCrc);
                return false;
            }
        } else {
            buffer = NULL;
        }

        switch (header.type) {
        case 'C': { // Clear
            fpga_req_del_file(header.fid);
            break;
        }

        case 'F': { // File alias
            char *path = malloc(header.len + 1);
            memcpy(path, buffer, header.len);
            path[header.len] = '\x00';
            fpga_req_add_file_alias(header.fid, path);
            free(path);
            break;
        }

        case 'D': { // Data block
            fpga_req_add_file_data(header.fid, buffer, header.len);
            break;
        }

        case 'B': { // Bitstream
            done = true;
            break;
        }

        default:
            fpga_display_message(pax_buffer, ili9341, 0xa85a32, 0xFFFFFFFF,
                "Invalid packet type");
            return false;
        }

        if (!done)
            free(buffer);
    }

    // Bitstream ready, load it
    ili9341_deinit(ili9341);
    ili9341_select(ili9341, false);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    ili9341_select(ili9341, true);

    esp_err_t res = ice40_load_bitstream(ice40, buffer, header.len);
    free(buffer);
    if (res != ESP_OK) {
        ice40_disable(ice40);
        ili9341_init(ili9341);
        fpga_display_message(pax_buffer, ili9341, 0xa85a32, 0xFFFFFFFF,
                "FPGA download mode\nUpload failed: %d", res);
        fpga_uart_mess("uploading bitstream failed with %d\n", res);
        return false;
    }
    fpga_uart_mess("bitstream has uploaded\n");

    return true;
}

bool fpga_host(xQueueHandle buttonQueue, ICE40* ice40, pax_buf_t* pax_buffer, ILI9341* ili9341, bool enable_uart) {
    while (true) {
        bool work_done = false;

        if (enable_uart) {
            if (fpga_uart_sync()) {
                return true;
            }
        }
        
        esp_err_t res;

        work_done |= fpga_btn_forward_events(ice40, buttonQueue, &res);
        if (res != ESP_OK) {
            ice40_disable(ice40);
            ili9341_init(ili9341);
            fpga_display_message(pax_buffer, ili9341, 0xa85a32, 0xFFFFFFFF,
                "FPGA download mode\nBTN error: %d", res);
            if (enable_uart) fpga_uart_mess("processing buttons events failed with %d\n", res);
            return false;
        }

        fpga_req_process(ice40, work_done ? 0 : (50 / portTICK_PERIOD_MS), &res);
        if (res != ESP_OK) {
            ice40_disable(ice40);
            ili9341_init(ili9341);
            fpga_display_message(pax_buffer, ili9341, 0xa85a32, 0xFFFFFFFF,
                "FPGA download mode\nREQ error: %d", res);
            if (enable_uart) fpga_uart_mess("processing fpga requests failed with %d\n", res);
            return false;
        }
    }
}

void fpga_download(xQueueHandle buttonQueue, ICE40* ice40, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    fpga_display_message(pax_buffer, ili9341, 0x325aa8, 0xFFFFFFFF,
        "FPGA download mode\nPreparing...");

    fpga_install_uart();
    fpga_irq_setup(ice40);
    fpga_req_setup();
    fpga_btn_reset();

    ice40_disable(ice40);
    ili9341_init(ili9341);

    uint8_t  counter = 0;

    while (!fpga_uart_sync())
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

        if (!fpga_uart_download(ice40, pax_buffer, ili9341))
            goto error;

        // Waiting for next download and sending key strokes to FPGA
        bool uart_triggered = fpga_host(buttonQueue, ice40, pax_buffer, ili9341, true);
        if (!uart_triggered) goto error;
        ice40_disable(ice40);
        ili9341_init(ili9341);
    }

    return;

error:
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    fpga_req_cleanup();
    fpga_irq_cleanup(ice40);
    fpga_uninstall_uart();
    return;
}
