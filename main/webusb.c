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
#include <nvs.h>
#include <nvs_flash.h>

#include "app_management.h"
#include "appfs.h"
#include "appfs_wrapper.h"
#include "driver/uart.h"
#include "driver_fsoverbus.h"
#include "esp32/rom/crc.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "filesystems.h"
#include "graphics_wrapper.h"
#include "hardware.h"
#include "ice40.h"
#include "managed_i2c.h"
#include "pax_gfx.h"
#include "system_wrapper.h"
#include "terminal.h"

typedef enum { STATE_WAITING, STATE_RECEIVING_HEADER, STATE_RECEIVING_PAYLOAD, STATE_PROCESS } webusb_state_t;

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

const int webusb_max_payload_size = 8192;
const int webusb_packet_buffer_size = sizeof(webusb_packet_header_t) + webusb_max_payload_size;

#define WEBUSB_UART                UART_NUM_0
#define WEBUSB_UART_QUEUE_DEPTH    (32)

static QueueHandle_t uart0_queue = NULL;

static const uint32_t webusb_packet_magic   = 0xFEEDF00D;
static const uint32_t webusb_response_error = (('E' << 0) | ('R' << 8) | ('R' << 16) | ('0' << 24));

static FILE*    file_fd    = NULL;
static uint32_t file_write = false;

static appfs_handle_t appfs_handle = APPFS_INVALID_FD;
static uint32_t appfs_position = 0;
static int appfs_size = 0;

// Generic
#define WEBUSB_CMD_SYNC (('S' << 0) | ('Y' << 8) | ('N' << 16) | ('C' << 24))  // Echo back empty response
#define WEBUSB_CMD_PING (('P' << 0) | ('I' << 8) | ('N' << 16) | ('G' << 24))  // Echo payload back to PC
// FAT FS
#define WEBUSB_CMD_FSLS (('F' << 0) | ('S' << 8) | ('L' << 16) | ('S' << 24))  // List files
#define WEBUSB_CMD_FSEX (('F' << 0) | ('S' << 8) | ('E' << 16) | ('X' << 24))  // Check if file exists
#define WEBUSB_CMD_FSMD (('F' << 0) | ('S' << 8) | ('M' << 16) | ('D' << 24))  // Create directory
#define WEBUSB_CMD_FSRM (('F' << 0) | ('S' << 8) | ('R' << 16) | ('M' << 24))  // Remove tree from filesystem
#define WEBUSB_CMD_FSST (('F' << 0) | ('S' << 8) | ('S' << 16) | ('T' << 24))  // Read filesystem state
#define WEBUSB_CMD_FSFW (('F' << 0) | ('S' << 8) | ('F' << 16) | ('W' << 24))  // Open file for writing
#define WEBUSB_CMD_FSFR (('F' << 0) | ('S' << 8) | ('F' << 16) | ('R' << 24))  // Open file for reading
// Generic data transfer functions (used for both FAT FS files & AppFS)
#define WEBUSB_CMD_FSFC (('F' << 0) | ('S' << 8) | ('F' << 16) | ('C' << 24))  // Close file or app
#define WEBUSB_CMD_CHNK (('C' << 0) | ('H' << 8) | ('N' << 16) | ('K' << 24))  // Send / receive a block of data
// AppFS
#define WEBUSB_CMD_APPL (('A' << 0) | ('P' << 8) | ('P' << 16) | ('L' << 24))  // List apps
#define WEBUSB_CMD_APPR (('A' << 0) | ('P' << 8) | ('P' << 16) | ('R' << 24))  // Open app for reading
#define WEBUSB_CMD_APPW (('A' << 0) | ('P' << 8) | ('P' << 16) | ('W' << 24))  // Open app for writing
#define WEBUSB_CMD_APPD (('A' << 0) | ('P' << 8) | ('P' << 16) | ('D' << 24))  // Delete app
// NVS
#define WEBUSB_CMD_NVSL (('N' << 0) | ('V' << 8) | ('S' << 16) | ('L' << 24))  // List values
#define WEBUSB_CMD_NVSR (('N' << 0) | ('V' << 8) | ('S' << 16) | ('R' << 24))  // Read value from NVS
#define WEBUSB_CMD_NVSW (('N' << 0) | ('V' << 8) | ('S' << 16) | ('W' << 24))  // Write value to NVS
#define WEBUSB_CMD_NVSD (('N' << 0) | ('V' << 8) | ('S' << 16) | ('D' << 24))  // Delete value from NVS
// Responses
#define WEBUSB_ANS_OKOK (('O' << 0) | ('K' << 8) | ('O' << 16) | ('K' << 24))

bool webusb_close_files() {
    bool closed = false;
    if (appfs_handle != APPFS_INVALID_FD) {
        appfsClose(appfs_handle);
        closed = true;
    }
    if (file_fd != NULL) {
        fclose(file_fd);
        closed = true;
    }
    file_fd = NULL;
    appfs_handle = APPFS_INVALID_FD;
    appfs_position = 0;
    appfs_size = 0;
    file_write = false;
    return closed;
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
    ESP_ERROR_CHECK(uart_driver_install(WEBUSB_UART, webusb_packet_buffer_size, webusb_packet_buffer_size, WEBUSB_UART_QUEUE_DEPTH, &uart0_queue, 0));
    uart_config_t uart_config = {
        .baud_rate  = 921600,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    ESP_ERROR_CHECK(uart_param_config(WEBUSB_UART, &uart_config));
}

void webusb_new_disable_uart() { uart_driver_delete(WEBUSB_UART); }

void webusb_main(xQueueHandle button_queue) {
    terminal_start();
    terminal_log("Starting FS over bus...");
    driver_fsoverbus_init(&terminal_log_wrapped);
    webusb_enable_uart();
}

void webusb_send_error(webusb_packet_header_t* header, uint8_t error) {
    webusb_response_header_t response = {
        .magic = webusb_packet_magic, .identifier = header->identifier, .response = webusb_response_error + ( error << 24 ), .payload_length = 0, .payload_crc = 0};
    uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
}

void webusb_fs_list(webusb_packet_header_t* header, uint8_t* payload) {
    char* path = (char*) payload;
    path[header->payload_length] = '\0';
    terminal_log("File system list");
    terminal_printf("%s", path);
    DIR* dir = opendir(path);
    if (dir == NULL) {
        terminal_log("Failed to open path");
        webusb_send_error(header, 5);
        return;
    }

    struct dirent* ent;
    size_t         response_length = 0;
    while ((ent = readdir(dir)) != NULL) {
        response_length += sizeof(unsigned char);  // d_type
        response_length += sizeof(uint32_t); // name length
        response_length += strlen(ent->d_name);    // d_name
        response_length += sizeof(int); // stat ok
        response_length += sizeof(uint32_t); // file size
        response_length += sizeof(uint64_t); // file modification timestamp
    }
    if (response_length > 0) {
        rewinddir(dir);

        uint8_t* response_buffer = malloc(response_length);
        if (response_buffer == NULL) {
            terminal_log("Malloc failed (response)");
            webusb_send_error(header, 4);
            closedir(dir);
            return;
        }

        size_t response_position = 0;

        while ((ent = readdir(dir)) != NULL) {
            unsigned char* type = &response_buffer[response_position];
            *type               = ent->d_type;
            response_position += sizeof(unsigned char);
            uint32_t* namelength = (uint32_t*) &response_buffer[response_position];
            *namelength = strlen(ent->d_name);
            response_position += sizeof(uint32_t);
            strcpy((char*) &response_buffer[response_position], ent->d_name);
            response_position += strlen(ent->d_name);
            struct stat sb = {0};
            char tpath[255];
            sprintf(tpath, path);
            if (path[strlen(path) - 1] != '/') {
                strcat(tpath, "/");
            }
            strcat(tpath, ent->d_name);
            int* statok = (int*) &response_buffer[response_position];
            response_position += sizeof(int);
            *statok = stat(tpath, &sb);
            uint32_t* filesize = (uint32_t*) &response_buffer[response_position];
            *filesize = sb.st_size;
            response_position += sizeof(uint32_t);
            uint64_t* mtime = (uint64_t*) &response_buffer[response_position];
            *mtime = sb.st_mtime;
            response_position += sizeof(uint64_t);
        }

        closedir(dir);
        webusb_response_header_t response = {.magic = webusb_packet_magic, .identifier = header->identifier, .response = header->command, .payload_length = response_length, .payload_crc = crc32_le(0, response_buffer, response_length)};
        uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
        uart_write_bytes(WEBUSB_UART, response_buffer, response_length);
    } else {
        closedir(dir);
        webusb_response_header_t response = {.magic = webusb_packet_magic, .identifier = header->identifier, .response = header->command, .payload_length = 0, .payload_crc = 0};
        uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
    }
}

void webusb_process_packet(webusb_packet_header_t* header, uint8_t* payload) {
    switch (header->command) {
        case WEBUSB_CMD_SYNC:
            {
                terminal_log("Sync");
                webusb_response_header_t response = {
                    .magic = webusb_packet_magic, .identifier = header->identifier, .response = header->command, .payload_length = 0, .payload_crc = 0};
                uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                break;
            }
        case WEBUSB_CMD_PING:
            {
                terminal_log("Ping");
                webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                     .identifier     = header->identifier,
                                                     .response       = header->command,
                                                     .payload_length = header->payload_length,
                                                     .payload_crc    = header->payload_crc};
                uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                uart_write_bytes(WEBUSB_UART, payload, header->payload_length);
                break;
            }
        case WEBUSB_CMD_FSLS:
            webusb_fs_list(header, payload);
            break;
        case WEBUSB_CMD_FSEX:
            {
                if (header->payload_length < 1 || header->payload_length >= webusb_max_payload_size - 1) {
                    webusb_send_error(header, 9);
                    return;
                }
                payload[header->payload_length + 1] = '\0';
                terminal_printf("Exists %s", (char*) payload);
                uint8_t result[1] = {false};
                FILE*   fd        = fopen((char*) payload, "rb");
                result[0]         = (fd != NULL);
                webusb_close_files();
                webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                    .identifier     = header->identifier,
                                                    .response       = header->command,
                                                    .payload_length = 1,
                                                    .payload_crc    = crc32_le(0, result, 1)};
                uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                uart_write_bytes(WEBUSB_UART, result, 1);
                break;
            }
        case WEBUSB_CMD_FSMD:
            {
                if (header->payload_length < 1 || header->payload_length >= webusb_max_payload_size - 1) {
                    webusb_send_error(header, 9);
                    return;
                }
                payload[header->payload_length + 1] = '\0';
                terminal_printf("Create dir %s", (char*) payload);
                uint8_t result[1];
                result[0]                         = create_dir((char*) payload);
                webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                     .identifier     = header->identifier,
                                                     .response       = header->command,
                                                     .payload_length = 1,
                                                     .payload_crc    = crc32_le(0, result, 1)};
                uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                uart_write_bytes(WEBUSB_UART, result, 1);
                break;
            }
        case WEBUSB_CMD_FSRM:
            {
                if (header->payload_length < 1 || header->payload_length >= webusb_max_payload_size - 1) {
                    webusb_send_error(header, 9);
                    return;
                }
                payload[header->payload_length + 1] = '\0';
                terminal_printf("Delete %s", (char*) payload);
                uint8_t result[1];
                result[0]                         = remove_recursive((char*) payload);
                webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                     .identifier     = header->identifier,
                                                     .response       = header->command,
                                                     .payload_length = 1,
                                                     .payload_crc    = crc32_le(0, result, 1)};
                uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                uart_write_bytes(WEBUSB_UART, result, 1);
                break;
            }
        case WEBUSB_CMD_FSST:
            {
                terminal_log("State");
                uint8_t   result[(sizeof(uint64_t) * 4) + sizeof(uint32_t)];
                uint64_t* internal_size = (uint64_t*) &result[sizeof(uint64_t) * 0];
                uint64_t* internal_free = (uint64_t*) &result[sizeof(uint64_t) * 1];
                uint64_t* sdcard_size   = (uint64_t*) &result[sizeof(uint64_t) * 2];
                uint64_t* sdcard_free   = (uint64_t*) &result[sizeof(uint64_t) * 3];
                uint32_t* appfs_free    = (uint32_t*) &result[sizeof(uint64_t) * 4];
                get_internal_filesystem_size_and_available(internal_size, internal_free);
                get_sdcard_filesystem_size_and_available(sdcard_size, sdcard_free);
                *appfs_free                       = appfsGetFreeMem();
                webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                     .identifier     = header->identifier,
                                                     .response       = header->command,
                                                     .payload_length = sizeof(webusb_response_header_t),
                                                     .payload_crc    = crc32_le(0, result, sizeof(webusb_response_header_t))};
                uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                uart_write_bytes(WEBUSB_UART, result, sizeof(result));
                break;
            }
        case WEBUSB_CMD_FSFW:
            {
                if (header->payload_length < 1 || header->payload_length >= webusb_max_payload_size - 1) {
                    webusb_send_error(header, 9);
                    return;
                }
                payload[header->payload_length + 1] = '\0';
                terminal_printf("Write file %s", (char*) payload);
                webusb_close_files();
                file_write = true;
                file_fd    = fopen((char*) payload, "wb");

                uint8_t result[1] = {0};
                if (file_fd != NULL) {
                    result[0] = 1;
                }

                webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                     .identifier     = header->identifier,
                                                     .response       = header->command,
                                                     .payload_length = 1,
                                                     .payload_crc    = crc32_le(0, result, 1)};
                uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                uart_write_bytes(WEBUSB_UART, result, 1);
                break;
            }
        case WEBUSB_CMD_FSFR:
            {
                if (header->payload_length < 1 || header->payload_length >= webusb_max_payload_size - 1) {
                    webusb_send_error(header, 9);
                    return;
                }
                payload[header->payload_length + 1] = '\0';
                terminal_printf("Read file %s", (char*) payload);
                webusb_close_files();
                file_write = false;
                file_fd    = fopen((char*) payload, "rb");

                uint8_t result[1] = {0};
                if (file_fd != NULL) {
                    result[0] = 1;
                }

                webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                     .identifier     = header->identifier,
                                                     .response       = header->command,
                                                     .payload_length = 1,
                                                     .payload_crc    = crc32_le(0, result, 1)};
                uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                uart_write_bytes(WEBUSB_UART, result, 1);
                break;
            }
        case WEBUSB_CMD_FSFC:
            {
                terminal_log("Close file");
                uint8_t result[1] = {0};
                result[0] = webusb_close_files();
                webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                     .identifier     = header->identifier,
                                                     .response       = header->command,
                                                     .payload_length = 1,
                                                     .payload_crc    = crc32_le(0, result, 1)};
                uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                uart_write_bytes(WEBUSB_UART, result, 1);
                break;
            }
        case WEBUSB_CMD_CHNK:
            {
                terminal_log("Chunk");
                if (file_fd == NULL && appfs_handle == APPFS_INVALID_FD) {
                    // No file open
                    webusb_send_error(header, 6);
                    return;
                }

                if ((!file_write) && (header->payload_length > 0)) {
                    // Data sent while reading
                    webusb_send_error(header, 7);
                    return;
                }

                if (file_write) {
                    // Writing
                    uint32_t length = 0;
                    if (file_fd != NULL) {
                        length = fwrite(payload, 1, header->payload_length, file_fd);
                    } else {
                        size_t maximum_size = appfs_size - appfs_position;
                        if (header->payload_length > maximum_size) {
                            webusb_send_error(header, 8);
                            return;
                        }
                        esp_err_t res = appfsWrite(appfs_handle, appfs_position, payload, header->payload_length);
                        if (res == ESP_OK) {
                            length = header->payload_length;
                            appfs_position += header->payload_length;
                        }
                    }
                    webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                         .identifier     = header->identifier,
                                                         .response       = header->command,
                                                         .payload_length = sizeof(size_t),
                                                         .payload_crc    = crc32_le(0, (uint8_t*) &length, sizeof(size_t))};
                    uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                    uart_write_bytes(WEBUSB_UART, (uint8_t*) &length, sizeof(uint32_t));
                } else {
                    uint8_t* data = malloc(webusb_max_payload_size);
                    if (data == NULL) {
                        webusb_send_error(header, 4);
                    } else {
                        size_t length = 0;
                        if (file_fd != NULL) {
                            length = fread(data, 1, webusb_max_payload_size, file_fd);
                        } else {
                            size_t maximum_size = appfs_size - appfs_position;
                            length = webusb_max_payload_size;
                            if (length > maximum_size) {
                                length = maximum_size;
                            }
                            esp_err_t res = appfsRead(appfs_handle, appfs_position, data, length);
                            if (res == ESP_OK) {
                                appfs_position += length;
                            } else {
                                length = 0;
                            }
                        }
                        webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                             .identifier     = header->identifier,
                                                             .response       = header->command,
                                                             .payload_length = length,
                                                             .payload_crc    = crc32_le(0, data, length)};
                        uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                        uart_write_bytes(WEBUSB_UART, data, length);
                        free(data);
                    }
                }

                break;
            }
        case WEBUSB_CMD_APPL:
            {
                int response_length = 0;
                appfs_handle_t handle = APPFS_INVALID_FD;
                while ((handle = appfsNextEntry(handle)) != APPFS_INVALID_FD) {
                    const char* name;
                    const char* title;
                    uint16_t version;
                    int size;
                    appfsEntryInfoExt(handle, &name, &title, &version, &size);
                    response_length += sizeof(uint16_t); // name length
                    response_length += strlen(name);
                    response_length += sizeof(uint16_t); // title length
                    response_length += strlen(title);
                    response_length += sizeof(uint16_t); // version
                    response_length += sizeof(int); // size
                }

                if (response_length > 0) {
                    uint8_t* response_buffer = malloc(response_length);
                    if (response_buffer == NULL) {
                        terminal_log("Malloc failed (response)");
                        webusb_send_error(header, 4);
                        return;
                    }

                    handle = APPFS_INVALID_FD;
                    int response_position = 0;
                    while ((handle = appfsNextEntry(handle)) != APPFS_INVALID_FD) {
                        const char* name;
                        const char* title;
                        uint16_t version;
                        int size;
                        appfsEntryInfoExt(handle, &name, &title, &version, &size);

                        uint16_t* response_name_length = (uint16_t*) &response_buffer[response_position];
                        *response_name_length = strlen(name);
                        response_position += sizeof(uint16_t);
                        memcpy((char*) &response_buffer[response_position], name, strlen(name));
                        response_position += strlen(name);

                        uint16_t* response_title_length = (uint16_t*) &response_buffer[response_position];
                        *response_title_length = strlen(title);
                        response_position += sizeof(uint16_t);
                        memcpy((char*) &response_buffer[response_position], title, strlen(title));
                        response_position += strlen(title);

                        uint16_t* response_version = (uint16_t*) &response_buffer[response_position];
                        *response_version = version;
                        response_position += sizeof(uint16_t);

                        int* response_size = (int*) &response_buffer[response_position];
                        *response_size = size;
                        response_position += sizeof(int); // size
                    }

                    webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                        .identifier     = header->identifier,
                                                        .response       = header->command,
                                                        .payload_length = response_length,
                                                        .payload_crc    = crc32_le(0, response_buffer, response_length)};
                    uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                    uart_write_bytes(WEBUSB_UART, response_buffer, response_length);
                } else {
                    webusb_response_header_t response = {.magic = webusb_packet_magic, .identifier = header->identifier, .response = header->command, .payload_length = 0, .payload_crc = 0};
                    uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                }
                break;
            }
        case WEBUSB_CMD_APPR:
            {
                if (header->payload_length < 1 || header->payload_length >= webusb_max_payload_size - 1) {
                    webusb_send_error(header, 9);
                    return;
                }
                payload[header->payload_length + 1] = '\0';
                uint8_t result[5] = {0};
                uint8_t* result_success = &result[0];
                int* result_size = (int*) &result[1];

                webusb_close_files();
                appfs_handle = appfsOpen((char*) payload);
                if (appfs_handle != APPFS_INVALID_FD) {
                    appfsEntryInfo(appfs_handle, NULL, &appfs_size);
                    *result_success = true;
                    *result_size = appfs_size;
                }

                webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                     .identifier     = header->identifier,
                                                     .response       = header->command,
                                                     .payload_length = sizeof(result),
                                                     .payload_crc    = crc32_le(0, result, sizeof(result))};
                uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                uart_write_bytes(WEBUSB_UART, result, sizeof(result));
                break;
            }
        case WEBUSB_CMD_APPW:
            {
                webusb_close_files();
                if (header->payload_length < 6) {
                    webusb_send_error(header, 9);
                    return;
                }
                uint8_t name_length = payload[0];
                char* payload_name = (char*) &payload[sizeof(uint8_t)];
                uint8_t title_length = payload[sizeof(uint8_t) + name_length];
                char* payload_title = (char*) &payload[sizeof(uint8_t) + name_length + sizeof(uint8_t)];
                int* payload_filesize = (int*) &payload[sizeof(uint8_t) + name_length + sizeof(uint8_t) + title_length];
                uint16_t* payload_version = (uint16_t*) &payload[sizeof(uint8_t) + name_length + sizeof(uint8_t) + title_length + sizeof(int)];

                if (name_length >= 48) { // Name too long
                    webusb_send_error(header, 10);
                    return;
                }

                if (title_length >= 64) { // Title too long
                    webusb_send_error(header, 11);
                    return;
                }

                char name[48] = "";
                strncpy(name, payload_name, name_length);
                char title[64] = "";
                strncpy(title, payload_title, title_length);
                appfs_size = *payload_filesize;

                uint8_t result[1] = {0};

                esp_err_t res = appfsCreateFileExt(name, title, *payload_version, appfs_size, &appfs_handle);

                if (res == ESP_OK) {
                    int roundedSize = (appfs_size + (SPI_FLASH_MMU_PAGE_SIZE - 1)) & (~(SPI_FLASH_MMU_PAGE_SIZE - 1));
                    res = appfsErase(appfs_handle, 0, roundedSize);
                    if (res == ESP_OK) {
                        result[0] = 1;
                    }
                }

                file_write = true;

                webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                     .identifier     = header->identifier,
                                                     .response       = header->command,
                                                     .payload_length = sizeof(result),
                                                     .payload_crc    = crc32_le(0, result, sizeof(result))};
                uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                uart_write_bytes(WEBUSB_UART, result, sizeof(result));
                break;
            }
            case WEBUSB_CMD_APPD:
            {
                if (header->payload_length < 1 || header->payload_length >= webusb_max_payload_size - 1) {
                    webusb_send_error(header, 9);
                    return;
                }
                payload[header->payload_length + 1] = '\0';

                uint8_t result[1] = {0};

                esp_err_t res = appfsDeleteFile((char*) payload);

                if (res == ESP_OK) result[0] = 1;

                webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                     .identifier     = header->identifier,
                                                     .response       = header->command,
                                                     .payload_length = sizeof(result),
                                                     .payload_crc    = crc32_le(0, result, sizeof(result))};
                uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                uart_write_bytes(WEBUSB_UART, result, sizeof(result));
                break;
            }
            case WEBUSB_CMD_NVSL:
            {
                if (header->payload_length < 1 || header->payload_length >= webusb_max_payload_size - 1) {
                    webusb_send_error(header, 9);
                    return;
                }
                payload[header->payload_length + 1] = '\0';

                nvs_iterator_t it = nvs_entry_find("nvs", (char*) payload, NVS_TYPE_ANY);
                while(it != NULL) {
                    nvs_entry_info_t info;
                    nvs_entry_info(it, &info);
                    it = nvs_entry_next(it);
                    printf("key '%s', type '%d' \n", info.key, info.type);
                }

                uint8_t result[1] = {0};
                webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                     .identifier     = header->identifier,
                                                     .response       = header->command,
                                                     .payload_length = sizeof(result),
                                                     .payload_crc    = crc32_le(0, result, sizeof(result))};
                uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                uart_write_bytes(WEBUSB_UART, result, sizeof(result));
                break;
            }
            case WEBUSB_CMD_NVSR:
            {
                // Not implemented yet
                uint8_t result[1] = {0};
                webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                     .identifier     = header->identifier,
                                                     .response       = header->command,
                                                     .payload_length = sizeof(result),
                                                     .payload_crc    = crc32_le(0, result, sizeof(result))};
                uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                uart_write_bytes(WEBUSB_UART, result, sizeof(result));
                break;
            }
            case WEBUSB_CMD_NVSW:
            {
                // Not implemented yet
                uint8_t result[1] = {0};
                webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                     .identifier     = header->identifier,
                                                     .response       = header->command,
                                                     .payload_length = sizeof(result),
                                                     .payload_crc    = crc32_le(0, result, sizeof(result))};
                uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                uart_write_bytes(WEBUSB_UART, result, sizeof(result));
                break;
            }
            case WEBUSB_CMD_NVSD:
            {
                // Not implemented yet
                uint8_t result[1] = {0};
                webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                     .identifier     = header->identifier,
                                                     .response       = header->command,
                                                     .payload_length = sizeof(result),
                                                     .payload_crc    = crc32_le(0, result, sizeof(result))};
                uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                uart_write_bytes(WEBUSB_UART, result, sizeof(result));
                break;
            }
        default:
            terminal_log("Unknown command");
            webusb_send_error(header, 3);
    }
}

static void uart_event_task(void* pvParameters) {
    webusb_state_t         state = STATE_WAITING;
    uint8_t                magic_buffer[4];
    webusb_packet_header_t packet_header           = {0};
    uint32_t               packet_header_position  = 0;
    uint8_t*               packet_payload          = malloc(webusb_max_payload_size);
    uint32_t               packet_payload_position = 0;
    uart_event_t           event;
    uint8_t*               dtmp = (uint8_t*) malloc(webusb_packet_buffer_size);
    for (;;) {
        // Waiting for UART event.
        if (xQueueReceive(uart0_queue, (void*) &event, (TickType_t) portMAX_DELAY)) {
            bzero(dtmp, webusb_packet_buffer_size);
            switch (event.type) {
                // Event of UART receving data
                case UART_DATA:
                    {
                        size_t position = 0;
                        uart_read_bytes(WEBUSB_UART, dtmp, event.size, portMAX_DELAY);
                        while (position < event.size) {

                            if (state == STATE_WAITING) {
                                for (; position < event.size;) {
                                    magic_buffer[0] = magic_buffer[1];
                                    magic_buffer[1] = magic_buffer[2];
                                    magic_buffer[2] = magic_buffer[3];
                                    magic_buffer[3] = dtmp[position];
                                    position++;
                                    if (*(uint32_t*) magic_buffer == webusb_packet_magic) {
                                        *(uint32_t*) magic_buffer = 0x00000000;
                                        packet_header_position    = 0;
                                        packet_payload_position   = 0;
                                        memset(&packet_header, 0, sizeof(webusb_packet_header_t));
                                        state = STATE_RECEIVING_HEADER;
                                        break;
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
                                    if (packet_header.payload_length > webusb_max_payload_size) {
                                        webusb_send_error(&packet_header, 1);
                                        state = STATE_WAITING;
                                    } else if (packet_header.payload_length > 0) {
                                        memset(packet_payload, 0, packet_header.payload_length);
                                        packet_payload_position = 0;
                                        state = STATE_RECEIVING_PAYLOAD;
                                    } else {
                                        state = STATE_PROCESS;
                                    }
                                }
                            }

                            if (state == STATE_RECEIVING_PAYLOAD) {
                                while ((position < event.size) && (packet_payload_position < packet_header.payload_length)) {
                                    packet_payload[packet_payload_position] = dtmp[position];
                                    packet_payload_position++;
                                    position++;
                                }

                                if (packet_payload_position >= packet_header.payload_length) {
                                    state = STATE_PROCESS;
                                }
                            }

                            if (state == STATE_PROCESS) {
                                uint32_t packet_payload_crc = crc32_le(0, packet_payload, packet_header.payload_length);
                                if (packet_payload_crc == packet_header.payload_crc) {
                                    webusb_process_packet(&packet_header, packet_payload);
                                } else {
                                    terminal_log("CRC error");
                                    terminal_printf(" A %08X", packet_header.payload_crc);
                                    terminal_printf(" B %08X", packet_payload_crc);
                                    //webusb_send_error(&packet_header, 2);
                                    webusb_process_packet(&packet_header, packet_payload);
                                }
                                state = STATE_WAITING;
                            }
                        }
                        break;
                    }
                // Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    terminal_log("uart hw fifo overflow");
                    uart_flush_input(WEBUSB_UART);
                    xQueueReset(uart0_queue);
                    break;
                // Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    terminal_log("uart ring buffer full");
                    uart_flush_input(WEBUSB_UART);
                    xQueueReset(uart0_queue);
                    break;
                // Event of UART RX break detected
                case UART_BREAK:
                    terminal_log("uart rx break");
                    break;
                // Event of UART parity check error
                case UART_PARITY_ERR:
                    terminal_log("uart parity error");
                    break;
                // Event of UART frame error
                case UART_FRAME_ERR:
                    terminal_log("uart frame error");
                    break;
                // Others
                default:
                    terminal_printf("unhandled uart event: %d", event.type);
                    break;
            }
        }
    }
    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}

void webusb_new_main(xQueueHandle button_queue) {
    terminal_start();
    terminal_printf("Starting...");
    webusb_new_enable_uart();
    terminal_printf("UART driver installed");
    xTaskCreate(uart_event_task, "uart_event_task", 20480, NULL, 12, NULL);
}
