#include <stdio.h>
#include <string.h>
#include <sdkconfig.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "eeprom.h"

static const char *TAG = "SAO";

EEPROM sao_eeprom = {
    .i2c_bus = 0,
    .i2c_address = 0x50
};

uint8_t testdata[] = {
    0x4c, 0x49, 0x46, 0x45, 0x10, 0x0a, 0x04, 0x00,  0x4d, 0x79, 0x20, 0x61, 0x6d, 0x61, 0x7a, 0x69,
    0x6e, 0x67, 0x20, 0x61, 0x64, 0x64, 0x6f, 0x6e,  0x61, 0x6d, 0x61, 0x7a, 0x69, 0x6e, 0x67, 0x2e,
    0x70, 0x79, 0x01, 0xff, 0xff, 0xff};

uint8_t cloud[] = {
    0x4c, 0x49, 0x46, 0x45, 0x05, 0x08, 0x04, 0x00,  0x63, 0x6C, 0x6F, 0x75, 0x64, 0x68, 0x61, 0x74,
    0x63, 0x68, 0x65, 0x72, 0x79, 0x00, 0x50, 0x08,  0x07
};

uint8_t cassette[] = {
    0x4c, 0x49, 0x46, 0x45, 0x08, 0x08, 0x04, 0x00,  0x63, 0x61, 0x73, 0x73, 0x65, 0x74, 0x74, 0x65,
    0x68, 0x61, 0x74, 0x63, 0x68, 0x65, 0x72, 0x79, 0x00, 0x50, 0x08,  0x07
};

uint8_t diskette[] = {
    0x4c, 0x49, 0x46, 0x45, 0x08, 0x08, 0x04, 0x00,  0x64, 0x69, 0x73, 0x6B, 0x65, 0x74, 0x74, 0x65,
    0x68, 0x61, 0x74, 0x63, 0x68, 0x65, 0x72, 0x79, 0x00, 0x50, 0x08,  0x07
};

esp_err_t sao_identify() {
    eeprom_write(&sao_eeprom, 0, diskette, sizeof(diskette));
    vTaskDelay(pdMS_TO_TICKS(200));
    uint8_t header[8];
    if (eeprom_read(&sao_eeprom, 0, header, sizeof(header)) != ESP_OK) {
        ESP_LOGI(TAG, "No EEPROM SAO detected");
        return ESP_FAIL;
    }
    if (memcmp(header, "LIFE", 4) == 0) {
        // https://badge.a-combinator.com/addons/addon-id/
        uint8_t name_length = header[4];
        uint8_t driver_length = header[5];
        uint8_t driver_data_length = header[6];
        size_t total_length = name_length + driver_length + driver_data_length;

        uint8_t* buffer = malloc(total_length);

        if (buffer == NULL) {
            return ESP_ERR_NO_MEM;
        }

        if (eeprom_read(&sao_eeprom, sizeof(header), buffer, total_length) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read SAO metadata");
            free(buffer);
            return ESP_FAIL;
        }

        char* name = malloc(name_length+1); // +1 for string terminator
        char* driver = malloc(driver_length+1); // +1 for string terminator
        uint8_t* driver_data = malloc(driver_data_length);

        if ((name == NULL) || (driver == NULL) || (driver_data == NULL)) {
            free(buffer);
            if (name != NULL) free(name);
            if (driver != NULL) free(driver);
            if (driver_data != NULL) free(driver_data);
            return ESP_ERR_NO_MEM;
        }

        memcpy(name, &buffer[0], name_length);
        name[name_length] = '\0';
        memcpy(driver, &buffer[name_length], driver_length);
        driver[driver_length] = '\0';
        memcpy(driver_data, &buffer[name_length + driver_length], driver_data_length);
        free(buffer);

        ESP_LOGI(TAG, "SAO with binary descriptor detected: \"%s\", driver: \"%s\", containing %u bytes of driver data", name, driver, driver_data_length);
        printf("SAO driver data: ");
        for (size_t index = 0; index < driver_data_length; index++) {
            printf("%02X ", driver_data[index]);
        }
        printf("\n");

        free(name);
        free(driver);
        free(driver_data);
    } else if (memcmp(header, "JSON", 4) == 0) {
        // https://badge.a-combinator.com/addons/addon-id/
        ESP_LOGI(TAG, "SAO with JSON descriptor detected");
    } else {
        ESP_LOGI(TAG, "Unformatted SAO or SAO with unsupported formatting detected");
    }
    return ESP_OK;
}
