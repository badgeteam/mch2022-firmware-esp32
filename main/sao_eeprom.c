#include "sao_eeprom.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "eeprom.h"

static const char* TAG = "SAO";

EEPROM sao_eeprom = {.i2c_bus = 0, .i2c_address = 0x50};

esp_err_t sao_identify(SAO* sao) {
    if (sao == NULL) return ESP_FAIL;
    memset(sao, 0, sizeof(SAO));
    sao->type = SAO_NONE;
    uint8_t header[8];
    if (eeprom_read(&sao_eeprom, 0, header, sizeof(header)) != ESP_OK) {
        return ESP_OK;
    }
    if (memcmp(header, "LIFE", 4) == 0) {
        // https://badge.a-combinator.com/addons/addon-id/
        uint8_t name_length        = header[4];
        uint8_t driver_length      = header[5];
        uint8_t driver_data_length = header[6];
        size_t  total_length       = name_length + driver_length + driver_data_length;

        uint8_t* buffer = malloc(total_length);

        if (buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate buffer");
            return ESP_FAIL;
        }

        if (eeprom_read(&sao_eeprom, sizeof(header), buffer, total_length) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read SAO metadata");
            free(buffer);
            return ESP_FAIL;
        }

        memset(sao, 0, sizeof(SAO));
        sao->driver_data_length = driver_data_length;
        sao->type               = SAO_BINARY;

        memcpy(sao->name, &buffer[0], name_length);
        memcpy(sao->driver, &buffer[name_length], driver_length);
        memcpy(sao->driver_data, &buffer[name_length + driver_length], driver_data_length);
        free(buffer);

        ESP_LOGI(TAG, "SAO with binary descriptor detected: \"%s\", driver: \"%s\", containing %u bytes of driver data", sao->name, sao->driver,
                 sao->driver_data_length);
        printf("SAO driver data: ");
        for (size_t index = 0; index < driver_data_length; index++) {
            printf("%02X ", sao->driver_data[index]);
        }
        printf("\n");
    } else if (memcmp(header, "JSON", 4) == 0) {
        // https://badge.a-combinator.com/addons/addon-id/
        ESP_LOGI(TAG, "SAO with JSON descriptor detected");
        sao->type = SAO_JSON;
    } else {
        ESP_LOGI(TAG, "Unformatted SAO or SAO with unsupported formatting detected");
        sao->type = SAO_UNFORMATTED;
    }
    return ESP_OK;
}

esp_err_t sao_write_raw(size_t offset, uint8_t* buffer, size_t buffer_length) { return eeprom_write(&sao_eeprom, offset, buffer, buffer_length); }
