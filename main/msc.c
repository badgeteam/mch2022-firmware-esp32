#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "app_management.h"
#include "appfs.h"
#include "appfs_wrapper.h"
#include "driver/sdmmc_host.h"
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
#include "sdcard.h"
#include "sdmmc_cmd.h"
#include "system_wrapper.h"
#include "terminal.h"
#include "webusb.h"

#define MSC_UART                UART_NUM_0
#define MSC_PACKET_BUFFER_SIZE  (16384)
#define MSC_UART_RX_BUFFER_SIZE (MSC_PACKET_BUFFER_SIZE * 2)
#define MSC_UART_TX_BUFFER_SIZE MSC_UART_RX_BUFFER_SIZE
#define MSC_UART_QUEUE_DEPTH    (20)

static QueueHandle_t uart0_queue = NULL;

sdmmc_card_t* card = NULL;

static const uint32_t msc_packet_magic   = 0xFEEDF00D;
static const uint32_t msc_response_error = (('E' << 0) | ('R' << 8) | ('R' << 16) | ('0' << 24));

typedef enum { STATE_WAITING, STATE_RECEIVING_HEADER, STATE_RECEIVING_PAYLOAD, STATE_PROCESS } msc_state_t;

typedef struct {
    uint32_t identifier;
    uint32_t command;
    uint32_t payload_length;
    uint32_t payload_crc;
} msc_packet_header_t;

typedef struct {
    uint32_t magic;
    uint32_t identifier;
    uint32_t response;
    uint32_t payload_length;
    uint32_t payload_crc;
} msc_response_header_t;

typedef struct {
    uint32_t lun;
    uint32_t lba;
    uint32_t offset;
    uint32_t length;
    uint8_t  data[0];
} msc_payload_t;

#define MSC_CMD_SYNC (('S' << 0) | ('Y' << 8) | ('N' << 16) | ('C' << 24))  // Echo back empty response
#define MSC_CMD_PING (('P' << 0) | ('I' << 8) | ('N' << 16) | ('G' << 24))  // Echo payload back to PC
#define MSC_CMD_READ (('R' << 0) | ('E' << 8) | ('A' << 16) | ('D' << 24))  // Read block
#define MSC_CMD_WRIT (('W' << 0) | ('R' << 8) | ('I' << 16) | ('T' << 24))  // Write block
#define MSC_ANS_OKOK (('O' << 0) | ('K' << 8) | ('O' << 16) | ('K' << 24))

const esp_partition_t* internal_fs_partition;
uint8_t                disk_data_buffer[MSC_PACKET_BUFFER_SIZE];

void msc_enable_uart() {
    // Make sure any data remaining in the hardware buffers is completely transmitted
    fflush(stdout);
    fsync(fileno(stdout));

    // Take control over the UART peripheral
    ESP_ERROR_CHECK(uart_driver_install(MSC_UART, MSC_UART_RX_BUFFER_SIZE, MSC_UART_TX_BUFFER_SIZE, MSC_UART_QUEUE_DEPTH, &uart0_queue, 0));
    uart_config_t uart_config = {
        .baud_rate  = 2000000,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    ESP_ERROR_CHECK(uart_param_config(MSC_UART, &uart_config));
}

void msc_new_disable_uart() { uart_driver_delete(MSC_UART); }

void msc_send_error(msc_packet_header_t* header, uint8_t error) {
    msc_response_header_t response = {
        .magic = msc_packet_magic, .identifier = header->identifier, .response = msc_response_error + (error << 24), .payload_length = 0, .payload_crc = 0};
    uart_write_bytes(MSC_UART, &response, sizeof(msc_response_header_t));
}

void msc_process_packet(msc_packet_header_t* header, uint8_t* payload) {
    switch (header->command) {
        case MSC_CMD_SYNC:
            {
                terminal_printf("SYNC");
                msc_response_header_t response = {
                    .magic = msc_packet_magic, .identifier = header->identifier, .response = header->command, .payload_length = 0, .payload_crc = 0};
                uart_write_bytes(MSC_UART, &response, sizeof(msc_response_header_t));
                break;
            }
        case MSC_CMD_PING:
            {
                terminal_printf("PING");
                msc_response_header_t response = {.magic          = msc_packet_magic,
                                                  .identifier     = header->identifier,
                                                  .response       = header->command,
                                                  .payload_length = header->payload_length,
                                                  .payload_crc    = header->payload_crc};
                uart_write_bytes(MSC_UART, &response, sizeof(msc_response_header_t));
                uart_write_bytes(MSC_UART, payload, header->payload_length);
                break;
            }
        case MSC_CMD_READ:
            {
                if (header->payload_length != sizeof(msc_payload_t)) {
                    terminal_printf("Invalid payload length");
                    msc_send_error(header, 4);
                    break;
                }

                msc_payload_t* readPayload = (msc_payload_t*) payload;

                if (readPayload->length > sizeof(disk_data_buffer)) {
                    terminal_printf("Requested size too long");
                    msc_send_error(header, 5);
                    break;
                }

                // terminal_printf("READ %u, %u, %u", readPayload->lba, readPayload->offset, readPayload->length);

                if (readPayload->lun == 0) {
                    // Internal memory
                    esp_err_t res =
                        esp_partition_read(internal_fs_partition, readPayload->lba * 512 + readPayload->offset, disk_data_buffer, readPayload->length);
                    if (res != ESP_OK) {
                        terminal_printf("Part read error %d", res);
                        msc_send_error(header, 7);
                    }
                } else if (readPayload->lun == 1) {
                    // SD card
                    /*esp_err_t res = sdmmc_read_sectors(card, disk_data_buffer, readPayload->lba, readPayload->length);
                    if (res != ESP_OK) {
                        terminal_printf("SD read error %d", res);
                        msc_send_error(header, 7);
                    }*/
                    memset(disk_data_buffer, 0, readPayload->length);
                } else {
                    // Invalid logical device
                    terminal_printf("Invalid LUN");
                    msc_send_error(header, 6);
                    break;
                }

                msc_response_header_t response = {.magic          = msc_packet_magic,
                                                  .identifier     = header->identifier,
                                                  .response       = header->command,
                                                  .payload_length = readPayload->length,
                                                  .payload_crc    = crc32_le(0, disk_data_buffer, readPayload->length)};
                uart_write_bytes(MSC_UART, &response, sizeof(msc_response_header_t));
                uart_write_bytes(MSC_UART, disk_data_buffer, readPayload->length);
                break;
            }
        case MSC_CMD_WRIT:
            {
                if (header->payload_length < sizeof(msc_payload_t)) {
                    terminal_printf("Invalid payload length");
                    msc_send_error(header, 4);
                    break;
                }

                msc_payload_t* writePayload = (msc_payload_t*) payload;
                uint8_t*       pData        = (uint8_t*) writePayload->data;

                if (header->payload_length != sizeof(msc_payload_t) + writePayload->length) {
                    terminal_printf("Data length not match payload length");
                    msc_send_error(header, 5);
                    break;
                }

                // terminal_printf("WRITE (%u) %u, %u, %u)", header->payload_length, writePayload->lba, writePayload->offset, writePayload->length);

                if (writePayload->lun == 0) {
                    esp_err_t res = esp_partition_write(internal_fs_partition, writePayload->lba * 512 + writePayload->offset, pData, writePayload->length);

                    if (res != ESP_OK) {
                        terminal_printf("Part write error %d", res);
                        msc_send_error(header, 7);
                    }
                } else if (writePayload->lun == 1) {
                    // SD card
                } else {
                    // Invalid logical device
                    terminal_printf("Invalid LUN");
                    msc_send_error(header, 6);
                    break;
                }

                msc_response_header_t response = {
                    .magic = msc_packet_magic, .identifier = header->identifier, .response = MSC_ANS_OKOK, .payload_length = 0, .payload_crc = 0};

                uart_write_bytes(MSC_UART, &response, sizeof(msc_response_header_t));
                break;
            }
        default:
            terminal_printf("Unknown command");
            msc_send_error(header, 3);
    }
}

static void uart_event_task(void* pvParameters) {
    msc_state_t         state = STATE_WAITING;
    uint8_t             magic_buffer[4];
    msc_packet_header_t packet_header           = {0};
    size_t              packet_header_position  = 0;
    uint8_t*            packet_payload          = NULL;
    size_t              packet_payload_position = 0;
    uart_event_t        event;
    uint8_t*            dtmp = (uint8_t*) malloc(MSC_PACKET_BUFFER_SIZE);
    for (;;) {
        // Waiting for UART event.
        if (xQueueReceive(uart0_queue, (void*) &event, (TickType_t) portMAX_DELAY)) {
            bzero(dtmp, MSC_PACKET_BUFFER_SIZE);
            switch (event.type) {
                // Event of UART receving data
                case UART_DATA:
                    {
                        uart_read_bytes(MSC_UART, dtmp, event.size, portMAX_DELAY);
                        size_t position = 0;
                        while (position < event.size) {
                            if (state == STATE_WAITING) {
                                for (; position < event.size;) {
                                    magic_buffer[0] = magic_buffer[1];
                                    magic_buffer[1] = magic_buffer[2];
                                    magic_buffer[2] = magic_buffer[3];
                                    magic_buffer[3] = dtmp[position];
                                    position++;
                                    if (*(uint32_t*) magic_buffer == msc_packet_magic) {
                                        *(uint32_t*) magic_buffer = 0x00000000;
                                        packet_header_position    = 0;
                                        packet_payload_position   = 0;
                                        memset(&packet_header, 0, sizeof(msc_packet_header_t));
                                        state = STATE_RECEIVING_HEADER;
                                        // terminal_printf("Received magic");
                                        break;
                                    } else {
                                        // terminal_printf("M %08X", *(uint32_t*) magic_buffer);
                                    }
                                }
                            }
                            if (state == STATE_RECEIVING_HEADER) {
                                uint8_t* packet_header_raw = (uint8_t*) &packet_header;
                                while ((position < event.size) && (packet_header_position < sizeof(msc_packet_header_t))) {
                                    packet_header_raw[packet_header_position] = dtmp[position];
                                    packet_header_position++;
                                    position++;
                                }
                                if (packet_header_position == sizeof(msc_packet_header_t)) {
                                    /*terminal_printf("Received header");
                                    terminal_printf("TID: %08X", packet_header.identifier);
                                    terminal_printf("CMD: %08X", packet_header.command);
                                    terminal_printf("LEN: %08X", packet_header.payload_length);
                                    terminal_printf("CRC: %08X", packet_header.payload_crc);*/
                                    if (packet_header.payload_length > 0) {
                                        packet_payload                               = malloc(packet_header.payload_length + 1);
                                        packet_payload[packet_header.payload_length] = '\0';  // NULL terminate strings
                                        if (packet_payload == NULL) {
                                            msc_send_error(&packet_header, 1);
                                            state = STATE_WAITING;
                                        } else {
                                            /*msc_response_header_t response = {.magic          = msc_packet_magic,
                                                                                 .identifier     = packet_header.identifier,
                                                                                 .response       = (('D' << 0) | ('A' << 8) | ('T' << 16) | ('A' << 24)),
                                                                                 .payload_length = 0,
                                                                                 .payload_crc    = 0};
                                            uart_write_bytes(MSC_UART, &response, sizeof(msc_response_header_t));*/
                                            state = STATE_RECEIVING_PAYLOAD;
                                        }
                                    } else {
                                        state = STATE_PROCESS;
                                    }
                                }
                            }
                            if (state == STATE_RECEIVING_PAYLOAD) {
                                size_t bytes_to_copy = event.size - position;
                                if (bytes_to_copy > packet_header.payload_length - packet_payload_position) {
                                    bytes_to_copy = packet_header.payload_length - packet_payload_position;
                                }

                                memcpy(&packet_payload[packet_payload_position], &dtmp[position], bytes_to_copy);

                                position += bytes_to_copy;
                                packet_payload_position += bytes_to_copy;

                                if (packet_payload_position == packet_header.payload_length) {
                                    state = STATE_PROCESS;
                                }
                            }
                            if (state == STATE_PROCESS) {
                                uint32_t packet_payload_crc = crc32_le(0, packet_payload, packet_header.payload_length);
                                if (packet_payload_crc == packet_header.payload_crc) {
                                    msc_process_packet(&packet_header, packet_payload);
                                } else {
                                    terminal_printf("CRC error");
                                    terminal_printf(" > %08X", packet_header.payload_crc);
                                    terminal_printf(" C %08X", packet_payload_crc);
                                    terminal_printf(" S %u", packet_header.payload_length);

                                    char buf[64] = {0};
                                    int  p       = 0;
                                    for (int i = 0; i < packet_header.payload_length; i++) {
                                        sprintf(buf + p, "%02X", packet_payload[i]);
                                        p += 2;
                                        if (p >= 16) {
                                            terminal_printf("%s", buf);
                                            memset(buf, 0, sizeof(buf));
                                            p = 0;
                                        }
                                    }
                                    terminal_printf("%s", buf);

                                    msc_send_error(&packet_header, 2);
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
                // Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    terminal_printf("uart hw fifo overflow");
                    uart_flush_input(MSC_UART);
                    xQueueReset(uart0_queue);
                    break;
                // Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    terminal_printf("uart ring buffer full");
                    uart_flush_input(MSC_UART);
                    xQueueReset(uart0_queue);
                    break;
                // Event of UART RX break detected
                case UART_BREAK:
                    terminal_printf("uart rx break");
                    break;
                // Event of UART parity check error
                case UART_PARITY_ERR:
                    terminal_printf("uart parity error");
                    break;
                // Event of UART frame error
                case UART_FRAME_ERR:
                    terminal_printf("uart frame error");
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

void msc_main(xQueueHandle button_queue) {
    terminal_start();
    terminal_printf("Starting mass storage...");
    msc_enable_uart();
    xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, 12, NULL);

    if (get_internal_mounted()) {
        unmount_internal_filesystem();
    }

    /*if (get_sdcard_mounted()) {
        unmount_sdcard_filesystem();
    }*/

    internal_fs_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "locfd");
    if (internal_fs_partition == NULL) {
        terminal_printf("Internal partition not found");
        return;
    }
    uint32_t first_sector      = internal_fs_partition->address / 512;  // SPI_FLASH_SEC_SIZE;
    uint32_t amount_of_sectors = internal_fs_partition->size / 512;     // SPI_FLASH_SEC_SIZE;

    RP2040* rp2040 = get_rp2040();

    rp2040_set_msc_block_count(rp2040, 0, amount_of_sectors);
    rp2040_set_msc_block_size(rp2040, 0, 512);  // SPI_FLASH_SEC_SIZE);

    bool sdOk = false;
    if (get_sdcard_mounted()) {
        terminal_printf("SD card ready");
        card = getCard();
        rp2040_set_msc_block_count(rp2040, 1, card->csd.capacity);
        rp2040_set_msc_block_size(rp2040, 1, card->csd.sector_size);
        sdOk = true;
    } else {
        terminal_printf("SD card not ready");
    }

    terminal_printf("Mass storage ready!");

    uint8_t msc_control = sdOk ? 0x07 : 0x03;     // Bit 0: enable, bit 1: internal memory ready, bit 2: SD card ready
    rp2040_set_msc_control(rp2040, msc_control);  // Signal ready*/
}
