#include "webusb.h"

#include <errno.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

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
#include "rtc_memory.h"
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

const int webusb_max_payload_size   = 8192;
const int webusb_packet_buffer_size = sizeof(webusb_packet_header_t) + webusb_max_payload_size;

#define WEBUSB_UART             UART_NUM_0
#define WEBUSB_UART_QUEUE_DEPTH (32)

#define WEBUSB_PROTOCOL_VERSION (0x0002)

static QueueHandle_t uart0_queue = NULL;

static const uint32_t webusb_packet_magic   = 0xFEEDF00D;
static const uint32_t webusb_response_error = (('E' << 0) | ('R' << 8) | ('R' << 16) | ('0' << 24));

static FILE*    file_fd    = NULL;
static uint32_t file_write = false;

static appfs_handle_t appfs_handle   = APPFS_INVALID_FD;
static uint32_t       appfs_position = 0;
static int            appfs_size     = 0;

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
#define WEBUSB_CMD_APPX (('A' << 0) | ('P' << 8) | ('P' << 16) | ('X' << 24))  // Start app
// NVS
#define WEBUSB_CMD_NVSL (('N' << 0) | ('V' << 8) | ('S' << 16) | ('L' << 24))  // List values
#define WEBUSB_CMD_NVSR (('N' << 0) | ('V' << 8) | ('S' << 16) | ('R' << 24))  // Read value from NVS
#define WEBUSB_CMD_NVSW (('N' << 0) | ('V' << 8) | ('S' << 16) | ('W' << 24))  // Write value to NVS
#define WEBUSB_CMD_NVSD (('N' << 0) | ('V' << 8) | ('S' << 16) | ('D' << 24))  // Delete value from NVS

size_t webusb_nvs_get_size(char* namespace, char* key, nvs_type_t type) {
    if (type == NVS_TYPE_U8) return sizeof(uint8_t);
    if (type == NVS_TYPE_I8) return sizeof(int8_t);
    if (type == NVS_TYPE_U16) return sizeof(uint16_t);
    if (type == NVS_TYPE_I16) return sizeof(int16_t);
    if (type == NVS_TYPE_U32) return sizeof(uint32_t);
    if (type == NVS_TYPE_I32) return sizeof(int32_t);
    if (type == NVS_TYPE_U64) return sizeof(uint64_t);
    if (type == NVS_TYPE_I64) return sizeof(int64_t);
    if (type == NVS_TYPE_STR) {
        nvs_handle_t handle;
        if (nvs_open(namespace, NVS_READONLY, &handle) != ESP_OK) return 0;
        size_t size = 0;
        if (nvs_get_str(handle, key, NULL, &size) != ESP_OK) size = 0;
        nvs_close(handle);
        return size;
    }
    if (type == NVS_TYPE_BLOB) {
        nvs_handle_t handle;
        if (nvs_open(namespace, NVS_READONLY, &handle) != ESP_OK) return 0;
        size_t size = 0;
        if (nvs_get_blob(handle, key, NULL, &size) != ESP_OK) size = 0;
        nvs_close(handle);
        return size;
    }
    return 0;
}

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
    file_fd        = NULL;
    appfs_handle   = APPFS_INVALID_FD;
    appfs_position = 0;
    appfs_size     = 0;
    file_write     = false;
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
    webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                         .identifier     = header->identifier,
                                         .response       = webusb_response_error + (error << 24),
                                         .payload_length = 0,
                                         .payload_crc    = 0};
    uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
}

bool webusb_terminate_string(webusb_packet_header_t* header, uint8_t* payload) {
    if (header->payload_length < 1 || header->payload_length >= webusb_max_payload_size - 1) {
        webusb_send_error(header, 9);
        return false;
    }
    payload[header->payload_length] = '\0';
    return true;
}

void webusb_fs_list(webusb_packet_header_t* header, uint8_t* payload) {
    if (!webusb_terminate_string(header, payload)) return;

    DIR* dir = opendir((char*) (payload));
    if (dir == NULL) {
        webusb_send_error(header, 5);
        return;
    }

    struct dirent* ent;
    size_t         response_length = 0;
    while ((ent = readdir(dir)) != NULL) {
        response_length += sizeof(unsigned char);  // d_type
        response_length += sizeof(uint32_t);       // name length
        response_length += strlen(ent->d_name);    // d_name
        response_length += sizeof(int);            // stat ok
        response_length += sizeof(uint32_t);       // file size
        response_length += sizeof(uint64_t);       // file modification timestamp
    }
    if (response_length > 0) {
        rewinddir(dir);

        uint8_t* response_buffer = malloc(response_length);
        if (response_buffer == NULL) {
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
            *namelength          = strlen(ent->d_name);
            response_position += sizeof(uint32_t);
            strcpy((char*) &response_buffer[response_position], ent->d_name);
            response_position += strlen(ent->d_name);
            struct stat sb = {0};
            char        tpath[255];
            sprintf(tpath, (char*) (payload));
            if (payload[strlen((char*) (payload)) - 1] != '/') {
                strcat(tpath, "/");
            }
            strcat(tpath, ent->d_name);
            int* statok = (int*) &response_buffer[response_position];
            response_position += sizeof(int);
            *statok            = stat(tpath, &sb);
            uint32_t* filesize = (uint32_t*) &response_buffer[response_position];
            *filesize          = sb.st_size;
            response_position += sizeof(uint32_t);
            uint64_t* mtime = (uint64_t*) &response_buffer[response_position];
            *mtime          = sb.st_mtime;
            response_position += sizeof(uint64_t);
        }

        closedir(dir);
        webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                             .identifier     = header->identifier,
                                             .response       = header->command,
                                             .payload_length = response_length,
                                             .payload_crc    = crc32_le(0, response_buffer, response_length)};
        uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
        uart_write_bytes(WEBUSB_UART, response_buffer, response_length);
    } else {
        closedir(dir);
        webusb_response_header_t response = {
            .magic = webusb_packet_magic, .identifier = header->identifier, .response = header->command, .payload_length = 0, .payload_crc = 0};
        uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
    }
}

void webusb_process_packet(webusb_packet_header_t* header, uint8_t* payload) {
    switch (header->command) {
        case WEBUSB_CMD_SYNC:
            {
                uint16_t                 result   = WEBUSB_PROTOCOL_VERSION;
                webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                     .identifier     = header->identifier,
                                                     .response       = header->command,
                                                     .payload_length = sizeof(result),
                                                     .payload_crc    = crc32_le(0, (uint8_t*) &result, sizeof(result))};
                uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                uart_write_bytes(WEBUSB_UART, (uint8_t*) &result, sizeof(result));
                break;
            }
        case WEBUSB_CMD_PING:
            {
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
                if (!webusb_terminate_string(header, payload)) return;
                uint8_t result[1] = {false};
                FILE*   fd        = fopen((char*) payload, "rb");
                result[0]         = (fd != NULL);
                if (fd != NULL) {
                    fclose(fd);
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
        case WEBUSB_CMD_FSMD:
            {
                if (!webusb_terminate_string(header, payload)) return;
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
                if (!webusb_terminate_string(header, payload)) return;
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
                uint8_t   result[sizeof(uint64_t) * 6];
                uint64_t* internal_size = (uint64_t*) &result[sizeof(uint64_t) * 0];
                uint64_t* internal_free = (uint64_t*) &result[sizeof(uint64_t) * 1];
                uint64_t* sdcard_size   = (uint64_t*) &result[sizeof(uint64_t) * 2];
                uint64_t* sdcard_free   = (uint64_t*) &result[sizeof(uint64_t) * 3];
                uint64_t* appfs_size    = (uint64_t*) &result[sizeof(uint64_t) * 4];
                uint64_t* appfs_free    = (uint64_t*) &result[sizeof(uint64_t) * 5];
                get_internal_filesystem_size_and_available(internal_size, internal_free);
                get_sdcard_filesystem_size_and_available(sdcard_size, sdcard_free);
                *appfs_size                       = appfsGetTotalMem();
                *appfs_free                       = appfsGetFreeMem();
                webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                     .identifier     = header->identifier,
                                                     .response       = header->command,
                                                     .payload_length = sizeof(result),
                                                     .payload_crc    = crc32_le(0, result, sizeof(result))};
                uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                uart_write_bytes(WEBUSB_UART, result, sizeof(result));
                break;
            }
        case WEBUSB_CMD_FSFW:
            {
                if (!webusb_terminate_string(header, payload)) return;
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
                if (!webusb_terminate_string(header, payload)) return;
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
                uint8_t result[1]                 = {0};
                result[0]                         = webusb_close_files();
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
                if (file_fd == NULL && appfs_handle == APPFS_INVALID_FD) {
                    // No file open
                    webusb_send_error(header, 6);
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
                    uint32_t requested_size = webusb_max_payload_size;
                    if (header->payload_length == 4) {
                        requested_size = *((uint32_t*) (payload));
                    } else if (header->payload_length != 0) {
                        webusb_send_error(header, 7);  // Data sent while reading
                        return;
                    }
                    if (requested_size < 1 || requested_size > webusb_max_payload_size) {
                        requested_size = webusb_max_payload_size;
                    }
                    uint8_t* data = malloc(requested_size);
                    if (data == NULL) {
                        webusb_send_error(header, 4);
                    } else {
                        size_t length = 0;
                        if (file_fd != NULL) {
                            length = fread(data, 1, requested_size, file_fd);
                        } else {
                            size_t maximum_size = appfs_size - appfs_position;
                            length              = requested_size;
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
                int            response_length = 0;
                appfs_handle_t handle          = APPFS_INVALID_FD;
                while ((handle = appfsNextEntry(handle)) != APPFS_INVALID_FD) {
                    const char* name;
                    const char* title;
                    uint16_t    version;
                    int         size;
                    appfsEntryInfoExt(handle, &name, &title, &version, &size);
                    response_length += sizeof(uint16_t);  // name length
                    response_length += strlen(name);
                    response_length += sizeof(uint16_t);  // title length
                    response_length += strlen(title);
                    response_length += sizeof(uint16_t);  // version
                    response_length += sizeof(uint32_t);  // size
                }

                if (response_length > 0) {
                    uint8_t* response_buffer = malloc(response_length);
                    if (response_buffer == NULL) {
                        webusb_send_error(header, 4);
                        return;
                    }

                    handle                = APPFS_INVALID_FD;
                    int response_position = 0;
                    while ((handle = appfsNextEntry(handle)) != APPFS_INVALID_FD) {
                        const char* name;
                        const char* title;
                        uint16_t    version;
                        int         size;
                        appfsEntryInfoExt(handle, &name, &title, &version, &size);

                        uint16_t* response_name_length = (uint16_t*) &response_buffer[response_position];
                        *response_name_length          = strlen(name);
                        response_position += sizeof(uint16_t);
                        memcpy((char*) &response_buffer[response_position], name, strlen(name));
                        response_position += strlen(name);

                        uint16_t* response_title_length = (uint16_t*) &response_buffer[response_position];
                        *response_title_length          = strlen(title);
                        response_position += sizeof(uint16_t);
                        memcpy((char*) &response_buffer[response_position], title, strlen(title));
                        response_position += strlen(title);

                        uint16_t* response_version = (uint16_t*) &response_buffer[response_position];
                        *response_version          = version;
                        response_position += sizeof(uint16_t);

                        uint32_t* response_size = (uint32_t*) &response_buffer[response_position];
                        *response_size          = size;
                        response_position += sizeof(uint32_t);  // size
                    }

                    webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                         .identifier     = header->identifier,
                                                         .response       = header->command,
                                                         .payload_length = response_length,
                                                         .payload_crc    = crc32_le(0, response_buffer, response_length)};
                    uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                    uart_write_bytes(WEBUSB_UART, response_buffer, response_length);
                } else {
                    webusb_response_header_t response = {
                        .magic = webusb_packet_magic, .identifier = header->identifier, .response = header->command, .payload_length = 0, .payload_crc = 0};
                    uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                }
                break;
            }
        case WEBUSB_CMD_APPR:
            {
                if (!webusb_terminate_string(header, payload)) return;
                uint8_t  result[5]      = {0};
                uint8_t* result_success = &result[0];
                int*     result_size    = (int*) &result[1];

                webusb_close_files();
                appfs_handle = appfsOpen((char*) payload);
                if (appfs_handle != APPFS_INVALID_FD) {
                    appfsEntryInfo(appfs_handle, NULL, &appfs_size);
                    *result_success = true;
                    *result_size    = appfs_size;
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
                if (header->payload_length < 8) {
                    webusb_send_error(header, 9);
                    return;
                }
                uint8_t   name_length      = payload[0];
                char*     payload_name     = (char*) &payload[sizeof(uint8_t)];
                uint8_t   title_length     = payload[sizeof(uint8_t) + name_length];
                char*     payload_title    = (char*) &payload[sizeof(uint8_t) + name_length + sizeof(uint8_t)];
                uint32_t* payload_filesize = (uint32_t*) &payload[sizeof(uint8_t) + name_length + sizeof(uint8_t) + title_length];
                uint16_t* payload_version  = (uint16_t*) &payload[sizeof(uint8_t) + name_length + sizeof(uint8_t) + title_length + sizeof(uint32_t)];

                if (name_length >= 48) {  // Name too long
                    webusb_send_error(header, 10);
                    return;
                }

                if (title_length >= 64) {  // Title too long
                    webusb_send_error(header, 11);
                    return;
                }

                char name[48] = {0};
                strncpy(name, payload_name, name_length);
                char title[64] = {0};
                strncpy(title, payload_title, title_length);
                appfs_size       = *payload_filesize;  // Global variable!
                appfs_position   = 0;                  // Global variable!
                uint16_t version = *payload_version;

                uint8_t result[1] = {0};

                esp_err_t res = appfsCreateFileExt(name, title, version, appfs_size, &appfs_handle);

                if (res == ESP_OK) {
                    int roundedSize = (appfs_size + (SPI_FLASH_MMU_PAGE_SIZE - 1)) & (~(SPI_FLASH_MMU_PAGE_SIZE - 1));
                    res             = appfsErase(appfs_handle, 0, roundedSize);
                    if (res == ESP_OK) {
                        result[0]  = 1;
                        file_write = true;
                    }
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
        case WEBUSB_CMD_APPD:
            {
                if (!webusb_terminate_string(header, payload)) return;

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
        case WEBUSB_CMD_APPX:
            {
                if (!webusb_terminate_string(header, payload)) return;

                uint8_t result[1] = {0};

                appfs_handle_t fd = appfsOpen((char*) payload);
                if (fd != APPFS_INVALID_FD) {
                    result[0] = 1;
                }

                if (header->payload_length > strlen((char*) payload) + 1) {  // If payload is longer than the length of the app name
                    char* command =
                        (char*) &payload[strlen((char*) payload) + 1];  // Skip past the first \0 terminator and threat the rest of the payload as command
                    rtc_memory_string_write(command);
                }

                webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                     .identifier     = header->identifier,
                                                     .response       = header->command,
                                                     .payload_length = sizeof(result),
                                                     .payload_crc    = crc32_le(0, result, sizeof(result))};
                uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                uart_write_bytes(WEBUSB_UART, result, sizeof(result));

                if (fd != APPFS_INVALID_FD) {
                    appfs_boot_app(fd);
                }
                break;
            }
        case WEBUSB_CMD_NVSL:
            {
                char* namespace = NULL;
                if (header->payload_length > 0) {
                    if (!webusb_terminate_string(header, payload)) return;
                    namespace = (char*) payload;
                }

                uint32_t       response_length = 0;
                nvs_iterator_t it              = nvs_entry_find("nvs", namespace, NVS_TYPE_ANY);
                while (it != NULL) {
                    nvs_entry_info_t info;
                    nvs_entry_info(it, &info);
                    it = nvs_entry_next(it);

                    uint16_t* response_namespace_name_length = (uint16_t*) &payload[response_length];
                    response_length += sizeof(uint16_t);
                    if (response_length >= webusb_max_payload_size) {
                        webusb_send_error(header, 12);
                        return;
                    }
                    *response_namespace_name_length = strlen(info.namespace_name);

                    char* response_namespace_name = (char*) &payload[response_length];
                    response_length += *response_namespace_name_length;
                    if (response_length >= webusb_max_payload_size) {
                        webusb_send_error(header, 12);
                        return;
                    }
                    strcpy(response_namespace_name, info.namespace_name);

                    uint16_t* response_key_length = (uint16_t*) &payload[response_length];
                    response_length += sizeof(uint16_t);
                    if (response_length >= webusb_max_payload_size) {
                        webusb_send_error(header, 12);
                        return;
                    }
                    *response_key_length = strlen(info.key);

                    char* response_key = (char*) &payload[response_length];
                    response_length += strlen(info.key);
                    if (response_length >= webusb_max_payload_size) {
                        webusb_send_error(header, 12);
                        return;
                    }
                    strcpy(response_key, info.key);

                    uint8_t* response_type = (uint8_t*) &payload[response_length];
                    response_length += sizeof(uint8_t);
                    if (response_length >= webusb_max_payload_size) {
                        webusb_send_error(header, 12);
                        return;
                    }
                    *response_type = (uint8_t) info.type;

                    uint32_t* response_size = (uint32_t*) &payload[response_length];
                    response_length += sizeof(uint32_t);
                    if (response_length >= webusb_max_payload_size) {
                        webusb_send_error(header, 12);
                        return;
                    }
                    *response_size = webusb_nvs_get_size(info.namespace_name, info.key, info.type);
                }

                webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                     .identifier     = header->identifier,
                                                     .response       = header->command,
                                                     .payload_length = response_length,
                                                     .payload_crc    = crc32_le(0, payload, response_length)};
                uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                uart_write_bytes(WEBUSB_UART, payload, response_length);
                break;
            }
        case WEBUSB_CMD_NVSR:
            {
                if (header->payload_length < 3) {
                    webusb_send_error(header, 9);
                    return;
                }
                uint8_t namespace_length  = payload[0];
                char*   payload_namespace = (char*) &payload[sizeof(uint8_t)];
                uint8_t key_length        = payload[sizeof(uint8_t) + namespace_length];
                char*   payload_key       = (char*) &payload[sizeof(uint8_t) + namespace_length + sizeof(uint8_t)];
                uint8_t payload_type      = payload[sizeof(uint8_t) + namespace_length + sizeof(uint8_t) + key_length];

                if (namespace_length >= 17 || namespace_length < 1) {  // Namespace name too long
                    webusb_send_error(header, 10);
                    return;
                }

                if (key_length >= 17 || key_length < 1) {  // Key too long
                    webusb_send_error(header, 11);
                    return;
                }

                char namespace_name[17] = {0};
                strncpy(namespace_name, payload_namespace, namespace_length);
                char key[17] = {0};
                strncpy(key, payload_key, key_length);
                nvs_type_t type = (nvs_type_t) payload_type;

                uint32_t     result_length = 0;
                nvs_handle_t handle;
                if (nvs_open(namespace_name, NVS_READONLY, &handle) == ESP_OK) {
                    if (type == NVS_TYPE_U8) {
                        uint8_t* data = (uint8_t*) payload;
                        if (nvs_get_u8(handle, key, data) == ESP_OK) {
                            result_length += sizeof(uint8_t);
                        }
                    } else if (type == NVS_TYPE_I8) {
                        int8_t* data = (int8_t*) payload;
                        if (nvs_get_i8(handle, key, data) == ESP_OK) {
                            result_length = sizeof(int8_t);
                        }
                    } else if (type == NVS_TYPE_U16) {
                        uint16_t* data = (uint16_t*) payload;
                        if (nvs_get_u16(handle, key, data) == ESP_OK) {
                            result_length = sizeof(uint16_t);
                        }
                    } else if (type == NVS_TYPE_I16) {
                        int16_t* data = (int16_t*) payload;
                        if (nvs_get_i16(handle, key, data) == ESP_OK) {
                            result_length = sizeof(int16_t);
                        }
                    } else if (type == NVS_TYPE_U32) {
                        uint32_t* data = (uint32_t*) payload;
                        if (nvs_get_u32(handle, key, data) == ESP_OK) {
                            result_length = sizeof(uint32_t);
                        }
                    } else if (type == NVS_TYPE_I32) {
                        int32_t* data = (int32_t*) payload;
                        if (nvs_get_i32(handle, key, data) == ESP_OK) {
                            result_length = sizeof(int32_t);
                        }
                    } else if (type == NVS_TYPE_U64) {
                        uint64_t* data = (uint64_t*) payload;
                        if (nvs_get_u64(handle, key, data) == ESP_OK) {
                            result_length = sizeof(uint64_t);
                        }
                    } else if (type == NVS_TYPE_I64) {
                        int64_t* data = (int64_t*) payload;
                        if (nvs_get_i64(handle, key, data) == ESP_OK) {
                            result_length = sizeof(int64_t);
                        }
                    } else if (type == NVS_TYPE_STR) {
                        char*  data          = (char*) payload;
                        size_t string_length = 0;
                        if (nvs_get_str(handle, key, NULL, &string_length) == ESP_OK) {
                            if (string_length <= webusb_max_payload_size) {
                                if (nvs_get_str(handle, key, data, &string_length) == ESP_OK) {
                                    result_length = string_length - 1;  // Remove \0 terminator from response data
                                }
                            }
                        }
                    } else if (type == NVS_TYPE_BLOB) {
                        void*  data        = (void*) payload;
                        size_t blob_length = 0;
                        if (nvs_get_blob(handle, key, NULL, &blob_length) == ESP_OK) {
                            if (blob_length <= webusb_max_payload_size) {
                                if (nvs_get_blob(handle, key, data, &blob_length) == ESP_OK) {
                                    result_length = blob_length;
                                }
                            }
                        }
                    }
                    nvs_close(handle);
                }

                webusb_response_header_t response = {.magic          = webusb_packet_magic,
                                                     .identifier     = header->identifier,
                                                     .response       = header->command,
                                                     .payload_length = result_length,
                                                     .payload_crc    = result_length ? crc32_le(0, payload, result_length) : 0};
                uart_write_bytes(WEBUSB_UART, &response, sizeof(webusb_response_header_t));
                if (result_length > 0) {
                    uart_write_bytes(WEBUSB_UART, payload, result_length);
                }
                break;
            }
        case WEBUSB_CMD_NVSW:
            {
                if (header->payload_length < 3 || header->payload_length >= webusb_max_payload_size) {
                    webusb_send_error(header, 9);
                    return;
                }

                uint32_t payload_position = 0;

                uint8_t namespace_length = payload[payload_position];
                payload_position += sizeof(uint8_t);

                char* payload_namespace = (char*) &payload[payload_position];
                payload_position += namespace_length;

                uint8_t key_length = payload[payload_position];
                payload_position += sizeof(uint8_t);

                char* payload_key = (char*) &payload[payload_position];
                payload_position += key_length;

                uint8_t payload_type = payload[payload_position];
                payload_position += sizeof(uint8_t);

                void*    data        = (void*) &payload[payload_position];
                uint32_t data_length = header->payload_length - payload_position;

                if (header->payload_length <= payload_position) {  // Request too short
                    webusb_send_error(header, 12);
                    return;
                }

                if (namespace_length >= 17 || namespace_length < 1) {  // Namespace name too long
                    webusb_send_error(header, 10);
                    return;
                }

                if (key_length >= 17 || key_length < 1) {  // Key too long
                    webusb_send_error(header, 11);
                    return;
                }

                char namespace_name[17] = {0};
                strncpy(namespace_name, payload_namespace, namespace_length);
                char key[17] = {0};
                strncpy(key, payload_key, key_length);
                nvs_type_t type = (nvs_type_t) payload_type;

                if (type == NVS_TYPE_STR) {
                    char* data_str        = (char*) data;
                    data_str[data_length] = '\0';
                }

                uint8_t result[1] = {0};

                nvs_handle_t handle;
                if (nvs_open(namespace_name, NVS_READWRITE, &handle) == ESP_OK) {
                    if ((type == NVS_TYPE_U8) && (data_length == sizeof(uint8_t)) && (nvs_set_u8(handle, key, *((uint8_t*) (data))) == ESP_OK)) {
                        result[0] = 1;
                    } else if ((type == NVS_TYPE_U8) && (data_length == sizeof(uint8_t)) && (nvs_set_u8(handle, key, *((uint8_t*) (data))) == ESP_OK)) {
                        result[0] = 1;
                    } else if ((type == NVS_TYPE_I8) && (data_length == sizeof(int8_t)) && (nvs_set_i8(handle, key, *((int8_t*) (data))) == ESP_OK)) {
                        result[0] = 1;
                    } else if ((type == NVS_TYPE_U16) && (data_length == sizeof(uint16_t)) && (nvs_set_u16(handle, key, *((uint16_t*) (data))) == ESP_OK)) {
                        result[0] = 1;
                    } else if ((type == NVS_TYPE_I16) && (data_length == sizeof(int16_t)) && (nvs_set_i16(handle, key, *((int16_t*) (data))) == ESP_OK)) {
                        result[0] = 1;
                    } else if ((type == NVS_TYPE_U32) && (data_length == sizeof(uint32_t)) && (nvs_set_u32(handle, key, *((uint32_t*) (data))) == ESP_OK)) {
                        result[0] = 1;
                    } else if ((type == NVS_TYPE_I32) && (data_length == sizeof(int32_t)) && (nvs_set_i32(handle, key, *((int32_t*) (data))) == ESP_OK)) {
                        result[0] = 1;
                    } else if ((type == NVS_TYPE_U64) && (data_length == sizeof(uint64_t)) && (nvs_set_u64(handle, key, *((uint64_t*) (data))) == ESP_OK)) {
                        result[0] = 1;
                    } else if ((type == NVS_TYPE_I64) && (data_length == sizeof(int64_t)) && (nvs_set_i64(handle, key, *((int64_t*) (data))) == ESP_OK)) {
                        result[0] = 1;
                    } else if ((type == NVS_TYPE_STR) && (nvs_set_str(handle, key, (char*) data) == ESP_OK)) {
                        result[0] = 1;
                    } else if ((type == NVS_TYPE_BLOB) && (nvs_set_blob(handle, key, data, data_length)) == ESP_OK) {
                        result[0] = 1;
                    }
                    nvs_close(handle);
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
        case WEBUSB_CMD_NVSD:
            {
                if (header->payload_length < 3) {
                    webusb_send_error(header, 9);
                    return;
                }

                uint32_t payload_position = 0;

                uint8_t namespace_length = payload[payload_position];
                payload_position += sizeof(uint8_t);

                char* payload_namespace = (char*) &payload[payload_position];
                payload_position += namespace_length;

                uint8_t key_length = payload[payload_position];
                payload_position += sizeof(uint8_t);

                char* payload_key = (char*) &payload[payload_position];
                payload_position += key_length;

                if (namespace_length >= 17 || namespace_length < 1) {  // Namespace name too long
                    webusb_send_error(header, 10);
                    return;
                }

                if (key_length >= 17 || key_length < 1) {  // Key too long
                    webusb_send_error(header, 11);
                    return;
                }

                char namespace_name[17] = {0};
                strncpy(namespace_name, payload_namespace, namespace_length);
                char key[17] = {0};
                strncpy(key, payload_key, key_length);

                uint8_t result[1] = {0};

                nvs_handle_t handle;
                if (nvs_open(namespace_name, NVS_READWRITE, &handle) == ESP_OK) {
                    if (nvs_erase_key(handle, key) == ESP_OK) {
                        result[0] = 1;
                    }
                    nvs_close(handle);
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
        default:
            webusb_send_error(header, 3);
    }
}

static void button_event_task(void* pvParameters) {
    RP2040*                rp2040       = get_rp2040();
    xQueueHandle           button_queue = rp2040->queue;
    rp2040_input_message_t input_message;
    for (;;) {
        if (xQueueReceive(button_queue, &input_message, (TickType_t) portMAX_DELAY)) {
            if (input_message.state && (input_message.input == RP2040_INPUT_BUTTON_HOME)) {
                pax_buf_t*        pax_buffer = get_pax_buffer();
                const pax_font_t* title_font = pax_font_saira_condensed;
                pax_background(pax_buffer, 0xFFFFFF);
                const char* text = "Disconnecting...";
                pax_vec1_t  dims = pax_text_size(title_font, 50, text);
                pax_center_text(pax_buffer, 0xFFE56B1A, title_font, 50, pax_buffer->width / 2, (pax_buffer->height - dims.y) / 2, text);
                display_flush();
                rp2040_exit_webusb_mode(rp2040);
                break;
            }
        }
    }
    vTaskDelete(NULL);
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

    if (packet_payload == NULL || dtmp == NULL) {
        printf("Fatal error: failed to start UART task");
        restart();
        return;
    }

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
                                        state                   = STATE_RECEIVING_PAYLOAD;
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
                                    webusb_send_error(&packet_header, 2);
                                }
                                state = STATE_WAITING;
                            }
                        }
                        break;
                    }
                // Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    uart_flush_input(WEBUSB_UART);
                    xQueueReset(uart0_queue);
                    break;
                // Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    uart_flush_input(WEBUSB_UART);
                    xQueueReset(uart0_queue);
                    break;
                // Event of UART RX break detected
                case UART_BREAK:
                    break;
                // Event of UART parity check error
                case UART_PARITY_ERR:
                    break;
                // Event of UART frame error
                case UART_FRAME_ERR:
                    break;
                // Others
                default:
                    break;
            }
        }
    }
    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}

void webusb_new_main(xQueueHandle button_queue) {
    RP2040*           rp2040            = get_rp2040();
    pax_buf_t*        pax_buffer        = get_pax_buffer();
    const pax_font_t* title_font        = pax_font_saira_condensed;
    const pax_font_t* instructions_font = pax_font_saira_regular;
    pax_background(pax_buffer, 0xFFFFFF);
    const char* text = "Connected to PC";
    pax_vec1_t  dims = pax_text_size(title_font, 50, text);
    pax_center_text(pax_buffer, 0xFFE56B1A, title_font, 50, pax_buffer->width / 2, (pax_buffer->height - dims.y) / 2, text);
    if (rp2040->_fw_version >= 0x0E) {  // Future coprocessor firmware will support this feature
        pax_draw_text(pax_buffer, 0xFF000000, instructions_font, 14, 5, pax_buffer->height - 17, "🅷 disconnect");
    }
    display_flush();
    webusb_new_enable_uart();
    xTaskCreate(uart_event_task, "uart_event_task", 20480, NULL, 12, NULL);
    if (rp2040->_fw_version >= 0x0E) {  // Future coprocessor firmware will support this feature
        xTaskCreate(button_event_task, "button_event_task", 2048, NULL, 12, NULL);
    }
}
