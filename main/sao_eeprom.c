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

    sao_binary_header_t header;
    if (eeprom_read(&sao_eeprom, 0, (uint8_t*) &header, sizeof(header)) != ESP_OK) {
        return ESP_OK;
    }

    if (memcmp(header.magic, "LIFE", 4) == 0) {
        // https://badge.a-combinator.com/addons/addon-id/
        uint8_t name_length        = header.name_length;
        uint8_t driver_name_length = header.driver_name_length;
        uint8_t driver_data_length = header.driver_data_length;
        size_t  total_length       = name_length + driver_name_length + driver_data_length;

        uint8_t* buffer = malloc(total_length);

        if (buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate buffer");
            return ESP_FAIL;
        }

        if (eeprom_read(&sao_eeprom, sizeof(sao_binary_header_t), buffer, total_length) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read SAO metadata");
            free(buffer);
            return ESP_FAIL;
        }

        memset(sao, 0, sizeof(SAO));
        sao->driver_data_length = driver_data_length;
        sao->type               = SAO_BINARY;

        memcpy(sao->name, &buffer[0], name_length);
        memcpy(sao->driver, &buffer[name_length], driver_name_length);
        memcpy(sao->driver_data, &buffer[name_length + driver_name_length], driver_data_length);
        free(buffer);

        ESP_LOGI(TAG, "SAO with binary descriptor detected: \"%s\", driver: \"%s\", containing %u bytes of driver data", sao->name, sao->driver,
                 sao->driver_data_length);
        printf("SAO driver data: ");
        for (size_t index = 0; index < driver_data_length; index++) {
            printf("%02X ", sao->driver_data[index]);
        }
        printf("\n");
    } else if (memcmp(header.magic, "JSON", 4) == 0) {
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

esp_err_t sao_format(const char* name, const char* driver, const uint8_t* driver_data, uint8_t driver_data_length, const char* driver2,
                     const uint8_t* driver2_data, uint8_t driver2_data_length) {
    uint8_t data[256];  // Should be more than enough :P

    size_t position = 0;

    sao_binary_header_t* header = (sao_binary_header_t*) data;
    memcpy(header->magic, "LIFE", 4);
    header->name_length        = strlen(name);
    header->driver_name_length = strlen(driver);
    header->driver_data_length = driver_data_length;
    header->number_of_drivers  = (driver2 == NULL) ? 0 : 1;
    position += sizeof(sao_binary_header_t);

    memcpy(&data[position], name, strlen(name));
    position += strlen(name);

    memcpy(&data[position], driver, strlen(driver));
    position += strlen(driver);

    memcpy(&data[position], driver_data, driver_data_length);
    position += driver_data_length;

    if (driver2 != NULL) {
        memcpy(&data[position], driver2, strlen(driver2));
        position += strlen(driver2);

        memcpy(&data[position], driver2_data, driver2_data_length);
        position += driver2_data_length;
    }

    sao_write_raw(0, data, position);
    return ESP_OK;
}
