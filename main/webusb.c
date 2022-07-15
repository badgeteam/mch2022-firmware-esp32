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

#define WEBUSB_UART UART_NUM_0
#define PATTERN_CHR_NUM (3)
#define WEBUSB_PACKET_BUFFER_SIZE (16384)
#define WEBUSB_UART_RX_BUFFER_SIZE (WEBUSB_PACKET_BUFFER_SIZE * 2)
#define WEBUSB_UART_TX_BUFFER_SIZE WEBUSB_UART_RX_BUFFER_SIZE
#define WEBUSB_UART_QUEUE_DEPTH (20)
#define LOG_LINES 12

static QueueHandle_t uart0_queue = NULL;
static QueueHandle_t log_queue = NULL;

char* log_lines[LOG_LINES] = {NULL};

static const uint32_t webusb_packet_magic = 0xFEEDF00D;
static const uint32_t webusb_response_error = 0xFFFFEE00;

typedef enum {
    STATE_WAITING,
    STATE_RECEIVING_HEADER,
    STATE_RECEIVING_PAYLOAD,
    STATE_PROCESS
} webusb_state_t;

typedef struct {
    uint32_t identifier;
    uint32_t command;
    uint32_t payload_length;
    uint32_t payload_crc;
} webusb_packet_header_t;

typedef struct {
    uint32_t magic;
    uint32_t identifier;
    uint32_t response;
    uint32_t payload_length;
    uint32_t payload_crc;
} webusb_response_header_t;

#define WEBUSB_CMD_SYNC (('S' << 0) | ('Y' << 8) | ('N' << 16) | ('C' << 24))
#define WEBUSB_CMD_PING (('P' << 0) | ('I' << 8) | ('N' << 16) | ('G' << 24))
#define WEBUSB_CMD_FSLS (('F' << 0) | ('S' << 8) | ('L' << 16) | ('S' << 24))

void webusb_log(char* fmt, ...) {
    char* buffer = malloc(256);
    if (buffer == NULL) return;
    buffer[255] = '\0';
    va_list va;
    va_start(va, fmt);
    vsnprintf(buffer, 255, fmt, va);
    va_end (va);
    xQueueSend(log_queue, &buffer, portMAX_DELAY);
}

typedef struct _log_task_args {
    xQueueHandle  button_queue;
    pax_buf_t*    pax_buffer;
    ILI9341*      ili9341;
    char*         lines[LOG_LINES];
    QueueHandle_t queue;
} log_task_args_t;

static void log_event_task(void *pvParameters) {
    log_task_args_t* args = (log_task_args_t*) pvParameters;
    pax_noclip(args->pax_buffer);
    for (;;) {
        char* buffer = NULL;
        if (xQueueReceive(args->queue, &buffer, portMAX_DELAY) == pdTRUE) {
            if (buffer != NULL) {
                if (args->lines[0] != NULL) {
                    free(args->lines[0]);
                    args->lines[0] = NULL;
                }
                for (uint8_t i = 0; i < LOG_LINES - 1; i++) {
                    args->lines[i] = args->lines[i + 1];
                }
                args->lines[LOG_LINES - 1] = buffer;
            }
            const pax_font_t* font = pax_font_sky_mono;
            pax_background(args->pax_buffer, 0xFFFFFF);
            for (uint8_t i = 0; i < LOG_LINES; i++) {
                if (args->lines[i] != NULL) {
                    pax_draw_text(args->pax_buffer, 0xFF000000, font, 18, 0, 20 * i, args->lines[i]);
                }
            }
            ili9341_write(args->ili9341, args->pax_buffer->buf); 
        }
    }
    vTaskDelete(NULL);
}

void webusb_print_status_wrapped(char* buffer) {
    xQueueSend(log_queue, &buffer, portMAX_DELAY);
}

void webusb_main(xQueueHandle button_queue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    log_task_args_t* log_task_args = malloc(sizeof(log_task_args_t));
    memset(log_task_args, 0, sizeof(log_task_args_t));
    log_task_args->button_queue = button_queue;
    log_task_args->pax_buffer = pax_buffer;
    log_task_args->ili9341 = ili9341;
    log_task_args->queue = xQueueCreate(8, sizeof(char*));
    xTaskCreate(log_event_task, "log_event_task", 2048, (void*) log_task_args, 12, NULL);
    log_queue = log_task_args->queue;
    webusb_log("Starting FS over bus...");
    driver_fsoverbus_init(&webusb_print_status_wrapped);
    webusb_enable_uart();
}

void webusb_enable_uart() {
    fflush(stdout);
    fsync(fileno(stdout));
    uart_set_pin(0, -1, -1, -1, -1);
    uart_set_pin(CONFIG_DRIVER_FSOVERBUS_UART_NUM, 1, 3, -1, -1);
}

void webusb_disable_uart() {
    uart_set_pin(0, 1, 3, -1, -1);
    uart_set_pin(CONFIG_DRIVER_FSOVERBUS_UART_NUM, -1, -1, -1, -1);
}

void webusb_new_enable_uart() {
    // Make sure any data remaining in the hardware buffers is completely transmitted
    fflush(stdout);
    fsync(fileno(stdout));
    
    // Take control over the UART peripheral
    ESP_ERROR_CHECK(uart_driver_install(WEBUSB_UART, WEBUSB_UART_RX_BUFFER_SIZE, WEBUSB_UART_TX_BUFFER_SIZE, WEBUSB_UART_QUEUE_DEPTH, &uart0_queue, 0));
    uart_config_t uart_config = {
        .baud_rate  = 115200,//921600,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    ESP_ERROR_CHECK(uart_param_config(WEBUSB_UART, &uart_config));
    
    //Set uart pattern detect function.
    uart_enable_pattern_det_baud_intr(WEBUSB_UART, '+', PATTERN_CHR_NUM, 9, 0, 0);
    //Reset the pattern queue length to record at most 20 pattern positions.
    uart_pattern_queue_reset(WEBUSB_UART, 20);
}

void webusb_new_disable_uart() {
    uart_driver_delete(WEBUSB_UART);
}

void webusb_send_error(webusb_packet_header_t* header, uint8_t error) {
    webusb_response_header_t response = {
        .magic = webusb_packet_magic,
        .identifier = header->identifier,
        .response = webusb_response_error & 3,
        .payload_length = 0,
        .payload_crc = 0
    };
    uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
}

void webusb_fs_list(webusb_packet_header_t* header, uint8_t* payload) {
    webusb_log("File system list");
    char* path = malloc(header->payload_length + 1);
    if (path == NULL) {
        webusb_log("Malloc failed (path)");
        webusb_send_error(header, 4);
        return;
    }

    memcpy(path, payload, header->payload_length);
    path[header->payload_length] = '\0';
    webusb_log("DIR: %s", path);
    DIR* dir = opendir(path);
    if (dir == NULL) {
        webusb_log("Failed to open %s", path);
        webusb_send_error(header, 5);
        return;
    }

    struct dirent* ent;
    size_t response_length = 0;
    while ((ent = readdir(dir)) != NULL) {
        response_length += sizeof(unsigned char); // d_type
        response_length += strlen(ent->d_name);   // d_name
        response_length += sizeof(size_t);
        response_length += sizeof(struct timespec) * 3;
        
    }
    rewinddir(dir);

    uint8_t* response_buffer = malloc(response_length);
    if (response_buffer == NULL) {
        webusb_log("Malloc failed (response)");
        webusb_send_error(header, 4);
        closedir(dir);
        return;
    }

    size_t response_position = 0;

    while ((ent = readdir(dir)) != NULL) {
        unsigned char* type = &response_buffer[response_position];
        *type = ent->d_type;
        response_position += sizeof(unsigned char);
        strcpy((char*) &response_buffer[response_position], ent->d_name);
        response_position += strlen(ent->d_name);
        //struct stat sb;
    }
    
    closedir(dir);
    
    webusb_response_header_t response = {
        .magic = webusb_packet_magic,
        .identifier = header->identifier,
        .response = header->command,
        .payload_length = response_length,
        .payload_crc = 0
    };
    uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
    uart_write_bytes(WEBUSB_UART, &response_buffer, response_length);
}

void webusb_process_packet(webusb_packet_header_t* header, uint8_t* payload) {
    switch(header->command) {
        case WEBUSB_CMD_SYNC: {
            webusb_log("Sync");
            webusb_response_header_t response = {
                .magic = webusb_packet_magic,
                .identifier = header->identifier,
                .response = header->command,
                .payload_length = 0,
                .payload_crc = 0
            };
            uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
            break;
        }
        case WEBUSB_CMD_PING: {
            webusb_log("Ping");
             webusb_response_header_t response = {
                .magic = webusb_packet_magic,
                .identifier = header->identifier,
                .response = header->command,
                .payload_length = 0,
                .payload_crc = 0
            };
            uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
            break;
        }
        case WEBUSB_CMD_FSLS:
            webusb_fs_list(header, payload);
            break;
        default:
            webusb_log("Unknown command");
            webusb_send_error(header, 3);
    }
}

static void uart_event_task(void *pvParameters) {
    webusb_state_t state = STATE_WAITING;
    uint8_t magic_buffer[4];
    webusb_packet_header_t packet_header = {0};
    size_t packet_header_position = 0;
    uint8_t* packet_payload = NULL;
    size_t packet_payload_position = 0;
    uart_event_t event;
    size_t buffered_size;
    uint8_t* dtmp = (uint8_t*) malloc(WEBUSB_PACKET_BUFFER_SIZE);
    for(;;) {
        //Waiting for UART event.
        if(xQueueReceive(uart0_queue, (void * )&event, (TickType_t)portMAX_DELAY)) {
            bzero(dtmp, WEBUSB_PACKET_BUFFER_SIZE);
            switch(event.type) {
                //Event of UART receving data
                case UART_DATA: {
                    webusb_log("RX %d bytes", event.size);
                    size_t position = 0;
                    while (position < event.size) {
                        if (state == STATE_WAITING) {
                            webusb_log("Waiting for magic (%d)", event.size);
                            uart_read_bytes(WEBUSB_UART, dtmp, event.size, portMAX_DELAY);
                            for (; position < event.size;) {
                                magic_buffer[0] = magic_buffer[1];
                                magic_buffer[1] = magic_buffer[2];
                                magic_buffer[2] = magic_buffer[3];
                                magic_buffer[3] = dtmp[position];
                                position++;
                                if (*(uint32_t*) magic_buffer == webusb_packet_magic) {
                                    *(uint32_t*) magic_buffer = 0x00000000;
                                    packet_header_position = 0;
                                    packet_payload_position = 0;
                                    memset(&packet_header, 0, sizeof(webusb_packet_header_t));
                                    state = STATE_RECEIVING_HEADER;
                                    webusb_log("Received magic");
                                    break;
                                } else {
                                    webusb_log("M %08X", *(uint32_t*) magic_buffer);
                                }
                            }
                        }
                        if (state == STATE_RECEIVING_HEADER) {
                            uint8_t* packet_header_raw = (uint8_t*) &packet_header;
                            while ((position < event.size) && (packet_header_position < sizeof(webusb_packet_header_t))) {
                                packet_header_raw[packet_header_position] = dtmp[position];
                                packet_header_position++;
                                position++;
                            }
                            if (packet_header_position == sizeof(webusb_packet_header_t)) {
                                webusb_log("Received header");
                                webusb_log("TID: %08X", packet_header.identifier);
                                webusb_log("CMD: %08X", packet_header.command);
                                webusb_log("LEN: %08X", packet_header.payload_length);
                                webusb_log("CRC: %08X", packet_header.payload_crc);
                                if (packet_header.payload_length > 0) {
                                    packet_payload = malloc(packet_header.payload_length);
                                    if (packet_payload == NULL) {
                                        webusb_send_error(&packet_header, 1);
                                        state = STATE_WAITING;
                                    } else {
                                        webusb_response_header_t response = {
                                            .magic = webusb_packet_magic,
                                            .identifier = packet_header.identifier,
                                            .response = 0xFFFFDA7A,
                                            .payload_length = 0,
                                            .payload_crc = 0
                                        };
                                        uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                                    state = STATE_RECEIVING_PAYLOAD;
                                    }
                                } else {
                                    state = STATE_PROCESS;
                                }
                            }
                        }
                        if (state == STATE_RECEIVING_PAYLOAD) {
                            webusb_log("Payload (%u+%d/%u)", packet_payload_position, event.size, packet_header.payload_length);
                            size_t bytes_to_copy = event.size - position;
                            if (bytes_to_copy > packet_header.payload_length - packet_payload_position) {
                                bytes_to_copy = packet_header.payload_length - packet_payload_position;
                            }
                            memcpy(packet_payload, &dtmp[position], bytes_to_copy);
                            position += bytes_to_copy;
                            packet_payload_position += bytes_to_copy;
                            if (packet_payload_position == packet_header.payload_length) {
                                state = STATE_PROCESS;
                            }
                        }
                        if (state == STATE_PROCESS) {
                            uint32_t packet_payload_crc = crc32_le(0, packet_payload, packet_header.payload_length);
                            if (packet_payload_crc == packet_header.payload_crc) {
                                webusb_process_packet(&packet_header, packet_payload);
                            } else {
                                webusb_log("CRC wrong:");
                                webusb_log("  H %08X", packet_header.payload_crc);
                                webusb_log("  C %08X", packet_payload_crc);
                                webusb_send_error(&packet_header, 2);
                                free(packet_payload);
                                packet_payload = NULL;
                            }
                            
                            if (packet_payload != NULL) {
                                free(packet_payload);
                                packet_payload = NULL;
                            }
                            state = STATE_WAITING;
                        }
                    }
                    break;
                }
                //Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    webusb_log("uart hw fifo overflow");
                    uart_flush_input(WEBUSB_UART);
                    xQueueReset(uart0_queue);
                    break;
                //Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    webusb_log("uart ring buffer full");
                    uart_flush_input(WEBUSB_UART);
                    xQueueReset(uart0_queue);
                    break;
                //Event of UART RX break detected
                case UART_BREAK:
                    webusb_log("uart rx break");
                    break;
                //Event of UART parity check error
                case UART_PARITY_ERR:
                    webusb_log("uart parity error");
                    break;
                //Event of UART frame error
                case UART_FRAME_ERR:
                    webusb_log("uart frame error");
                    break;
                //UART_PATTERN_DET
                case UART_PATTERN_DET:
                    uart_get_buffered_data_len(WEBUSB_UART, &buffered_size);
                    int pos = uart_pattern_pop_pos(WEBUSB_UART);
                    webusb_log("[UART PATTERN DETECTED] pos: %d, buffered size: %d", pos, buffered_size);
                    if (pos == -1) {
                        uart_flush_input(WEBUSB_UART);
                    } else {
                        uart_read_bytes(WEBUSB_UART, dtmp, pos, 100 / portTICK_PERIOD_MS);
                        uint8_t pat[PATTERN_CHR_NUM + 1];
                        memset(pat, 0, sizeof(pat));
                        uart_read_bytes(WEBUSB_UART, pat, PATTERN_CHR_NUM, 100 / portTICK_PERIOD_MS);
                        webusb_log("read data: %s", dtmp);
                        webusb_log("read pat : %s", pat);
                    }
                    break;
                //Others
                default:
                    webusb_log("unhandled uart event: %d", event.type);
                    break;
            }
        }
    }
    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}

void webusb_new_main(xQueueHandle button_queue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    log_task_args_t* log_task_args = malloc(sizeof(log_task_args_t));
    memset(log_task_args, 0, sizeof(log_task_args_t));
    log_task_args->button_queue = button_queue;
    log_task_args->pax_buffer = pax_buffer;
    log_task_args->ili9341 = ili9341;
    log_task_args->queue = xQueueCreate(8, sizeof(char*));
    xTaskCreate(log_event_task, "log_event_task", 2048, (void*) log_task_args, 12, NULL);
    log_queue = log_task_args->queue;
    webusb_log("Starting...");
    webusb_new_enable_uart();
    webusb_log("UART driver installed");
    xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, 12, NULL);
}
