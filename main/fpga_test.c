#include "fpga_test.h"
#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "fpga.h"
#include "ili9341.h"
#include "ice40.h"
#include "rp2040.h"
#include "hardware.h"

static const char *TAG = "fpga_test";

esp_err_t load_file_into_psram(ICE40* ice40, FILE* fd) {
    fseek(fd, 0, SEEK_SET);
    const uint8_t write_cmd = 0x02;
    uint32_t amount_read;
    uint32_t position = 0;
    uint8_t* tx_buffer = malloc(SPI_MAX_TRANSFER_SIZE);
    if (tx_buffer == NULL) return ESP_FAIL;

    while(1) {
        tx_buffer[0] = write_cmd;
        tx_buffer[1] = (position >> 16);
        tx_buffer[2] = (position >> 8) & 0xFF;
        tx_buffer[3] = position & 0xFF;
        amount_read = fread(&tx_buffer[4], 1, SPI_MAX_TRANSFER_SIZE - 4, fd);
        if (amount_read < 1) break;
        ESP_LOGI(TAG, "Writing PSRAM @ %u (%u bytes)", position, amount_read);
        esp_err_t res = ice40_transaction(ice40, tx_buffer, amount_read + 4, NULL, 0);
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Write transaction failed @ %u", position);
            free(tx_buffer);
            return res;
        }
        position += amount_read;
    };
    free(tx_buffer);
    return ESP_OK;
}

esp_err_t load_buffer_into_psram(ICE40* ice40, uint8_t* buffer, uint32_t buffer_length) {
    const uint8_t write_cmd = 0x02;
    uint32_t position = 0;
    uint8_t* tx_buffer = malloc(SPI_MAX_TRANSFER_SIZE);
    if (tx_buffer == NULL) return ESP_FAIL;
    while(1) {
        tx_buffer[0] = write_cmd;
        tx_buffer[1] = (position >> 16);
        tx_buffer[2] = (position >> 8) & 0xFF;
        tx_buffer[3] = position & 0xFF;
        uint32_t length = buffer_length - position;
        if (length > SPI_MAX_TRANSFER_SIZE - 4) length = SPI_MAX_TRANSFER_SIZE - 4;
        memcpy(&tx_buffer[4], &buffer[position], length);
        if (length == 0) break;
        ESP_LOGI(TAG, "Writing PSRAM @ %u (%u bytes)", position, length);
        esp_err_t res = ice40_transaction(ice40, tx_buffer, length + 4, NULL, 0);
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Write transaction failed @ %u", position);
            free(tx_buffer);
            return res;
        }
        position += length;
    };
    free(tx_buffer);
    return ESP_OK;
}

esp_err_t verify_file_in_psram(ICE40* ice40, FILE* fd) {
    fseek(fd, 0, SEEK_SET);
    const uint8_t read_cmd = 0x03;
    uint32_t amount_read;
    uint32_t position = 0;
    uint8_t* tx_buffer = malloc(SPI_MAX_TRANSFER_SIZE);
    if (tx_buffer == NULL) return ESP_FAIL;
    memset(tx_buffer, 0, SPI_MAX_TRANSFER_SIZE);
    uint8_t* verify_buffer = malloc(SPI_MAX_TRANSFER_SIZE);
    if (verify_buffer == NULL) return ESP_FAIL;
    uint8_t* rx_buffer = malloc(SPI_MAX_TRANSFER_SIZE);
    if (rx_buffer == NULL) return ESP_FAIL;

    while(1) {
        tx_buffer[0] = read_cmd;
        tx_buffer[1] = (position >> 16);
        tx_buffer[2] = (position >> 8) & 0xFF;
        tx_buffer[3] = position & 0xFF;
        amount_read = fread(&verify_buffer[4], 1, SPI_MAX_TRANSFER_SIZE - 4, fd);
        if (amount_read < 1) break;
        ESP_LOGI(TAG, "Reading PSRAM @ %u (%u bytes)", position, amount_read);
        esp_err_t res = ice40_transaction(ice40, tx_buffer, amount_read + 4, rx_buffer, amount_read + 4);
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Read transaction failed @ %u", position);
            free(tx_buffer);
            return res;
        }
        position += amount_read;
        ESP_LOGI(TAG, "Verifying PSRAM @ %u (%u bytes)", position, amount_read);
        for (uint32_t i = 4; i < amount_read; i++) {
            if (rx_buffer[i] != verify_buffer[i]) {
                ESP_LOGE(TAG, "Verifying PSRAM @ %u failed: %02X != %02X", position + i, rx_buffer[i], verify_buffer[i]);
                free(tx_buffer);
                free(rx_buffer);
                free(verify_buffer);
                return ESP_FAIL;
            }
        }
    };
    free(tx_buffer);
    free(rx_buffer);
    free(verify_buffer);
    ESP_LOGI(TAG, "PSRAM contents verified!");
    return ESP_OK;
}

void fpga_test(ILI9341* ili9341, ICE40* ice40, xQueueHandle buttonQueue) {
    esp_err_t res;
    bool reload_fpga = false;
    do {
        printf("Start FPGA test...\n");
        reload_fpga = false;
        printf("LCD deinit...\n");
        ili9341_deinit(ili9341);
        printf("LCD deselect...\n");
        ili9341_select(ili9341, false);
        printf("Wait...\n");
        vTaskDelay(200 / portTICK_PERIOD_MS);
        printf("LCD select...\n");
        ili9341_select(ili9341, true);

        printf("FPGA load...\n");
        res = ice40_load_bitstream(ice40, proto2_bin, proto2_bin_len);
        if (res != ESP_OK) {
            printf("Failed to load app bitstream into FPGA (%d)\n", res);
            return;
        } else {
            printf("Bitstream loaded succesfully!\n");
        }
        
        bool waitForChoice = true;
        while (waitForChoice) {
            rp2040_input_message_t buttonMessage = {0};
            printf("Waiting for button press...\n");
            if (xQueueReceive(buttonQueue, &buttonMessage, portMAX_DELAY) == pdTRUE) {
                printf("Button: %u, %u\n", buttonMessage.input, buttonMessage.state);
                if (buttonMessage.state) {
                    /*switch(buttonMessage.button) {
                        case PCA9555_PIN_BTN_HOME:
                        case PCA9555_PIN_BTN_MENU:
                        case PCA9555_PIN_BTN_BACK:
                            waitForChoice = false;
                            break;
                        case PCA9555_PIN_BTN_ACCEPT:
                            reload_fpga = true;
                            waitForChoice = false;
                            break;
                        default:
                            break;
                    }*/
                }
            }
        }
        ice40_disable(ice40);
        ili9341_init(ili9341);
    } while (reload_fpga);
}
