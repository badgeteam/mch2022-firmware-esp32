#include "sao_eeprom.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "eeprom.h"

static const char* TAG = "SAO";

EEPROM sao_eeprom_small = {.i2c_bus = 0, .i2c_address = 0x50, .address_16bit = false, .page_size = 16};
EEPROM sao_eeprom_big   = {.i2c_bus = 0, .i2c_address = 0x50, .address_16bit = true, .page_size = 64};

void dump_eeprom_contents(EEPROM* eeprom) {
    uint8_t buffer[128] = {0};
    if (eeprom_read(eeprom, 0, buffer, sizeof(buffer)) != ESP_OK) {
        printf("Failed to read EEPROM contents\n");
    } else {
        for (int i = 0; i < sizeof(buffer); i++) {
            if (i % 16 == 0) {
                printf("\n");
            }
            printf("%02x ", buffer[i]);
        }
    }
    printf("\n");
}

void restore_first_byte_of_small_eeprom(char data) {
    esp_err_t result = eeprom_write(&sao_eeprom_small, 0, (uint8_t*) &data, 1);
    if (result == ESP_OK) {
        printf("Restored first byte of small EEPROM\n");
    } else {
        printf("Failed to restore first byte of small EEPROM\n");
    }
}

esp_err_t sao_identify_binary(SAO* sao, EEPROM* eeprom, sao_binary_header_t* header) {
    // https://badge.a-combinator.com/addons/addon-id/

    if (header->magic[0] != 'L') {
        if (eeprom == &sao_eeprom_small) {
            ESP_LOGW(TAG, "SAO has corrupted first byte on small EEPROM, restoring...");
            restore_first_byte_of_small_eeprom('L');
        } else {
            ESP_LOGW(TAG, "SAO has corrupted first byte on big EEPROM");
        }
    }

    memset(sao, 0, sizeof(SAO));
    sao->type = SAO_BINARY;

    uint8_t  name_length = header->name_length;
    uint32_t position    = sizeof(sao_binary_header_t);

    if (eeprom_read(eeprom, position, (uint8_t*) sao->name, name_length) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read SAO name");
        return ESP_FAIL;
    }
    position += name_length;

    sao->name[name_length] = '\0';

    for (int i = 0; i < SAO_MAX_FIELD_LENGTH; i++) {
        if (sao->name[i] < ' ' && sao->name[i] > '\0') sao->name[i] = '?';
        if (sao->name[i] > '~') sao->name[i] = '?';
    }

    sao->amount_of_drivers = header->number_of_extra_drivers + 1;

    if (sao->amount_of_drivers > SAO_MAX_NUM_DRIVERS) {
        ESP_LOGW(TAG, "SAO %s has %u drivers, scanning at most %u driver definitions", sao->name, sao->amount_of_drivers, SAO_MAX_NUM_DRIVERS);
        sao->amount_of_drivers = SAO_MAX_NUM_DRIVERS;
    }

    uint8_t driver_name_length = header->driver_name_length;
    uint8_t driver_data_length = header->driver_data_length;

    for (uint8_t driver_index = 0; driver_index < sao->amount_of_drivers; driver_index++) {
        if (eeprom_read(eeprom, position, (uint8_t*) sao->drivers[driver_index].name, driver_name_length) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read SAO driver name");
            return ESP_FAIL;
        }
        position += driver_name_length;
        if (eeprom_read(eeprom, position, (uint8_t*) sao->drivers[driver_index].data, driver_data_length) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read SAO driver data");
            return ESP_FAIL;
        }
        position += driver_data_length;
        sao->drivers[driver_index].data_length = driver_data_length;

        for (int i = 0; i < SAO_MAX_FIELD_LENGTH; i++) {
            if (sao->drivers[driver_index].name[i] < ' ' && sao->drivers[driver_index].name[i] > '\0') sao->drivers[driver_index].name[i] = '?';
            if (sao->drivers[driver_index].name[i] > '~') sao->drivers[driver_index].name[i] = '?';
        }

        if (driver_index < sao->amount_of_drivers - 1) {
            sao_binary_extra_driver_t extra_header;
            if (eeprom_read(eeprom, position, (uint8_t*) &extra_header, sizeof(sao_binary_extra_driver_t)) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to read SAO extra driver header");
                return ESP_FAIL;
            }
            position += sizeof(sao_binary_extra_driver_t);
            driver_name_length = extra_header.driver_name_length;
            driver_data_length = extra_header.driver_data_length;
        }
    }

    ESP_LOGI(TAG, "SAO with binary descriptor detected: \"%s\" with %u drivers", sao->name, sao->amount_of_drivers);
    for (uint8_t driver_index = 0; driver_index < sao->amount_of_drivers; driver_index++) {
        printf("   #%u: \"%s\" with data ", driver_index + 1, sao->drivers[driver_index].name);
        for (size_t data_index = 0; data_index < sao->drivers[driver_index].data_length; data_index++) {
            printf("%02X ", sao->drivers[driver_index].data[data_index]);
        }
        printf("\n");
    }

    return ESP_OK;
}

esp_err_t sao_identify(SAO* sao) {
    if (sao == NULL) return ESP_FAIL;
    memset(sao, 0, sizeof(SAO));
    sao->type = SAO_NONE;
    sao_binary_header_t header;
    ESP_LOGI(TAG, "Identifying SAO (small EEPROM)...");
    dump_eeprom_contents(&sao_eeprom_small);
    esp_err_t result = eeprom_read(&sao_eeprom_small, 0, (uint8_t*) &header, sizeof(header));
    if (result != ESP_OK) {
        return ESP_OK;
    }
    if (memcmp(&header.magic[1], "IFE", 3) == 0) {
        ESP_LOGI(TAG, "SAO with binary descriptor on small EEPROM detected");
        return sao_identify_binary(sao, &sao_eeprom_small, &header);
    } else if (memcmp(&header.magic[1], "SON", 3) == 0) {
        // https://badge.a-combinator.com/addons/addon-id/
        ESP_LOGI(TAG, "SAO with JSON descriptor on small EEPROM detected");
        sao->type = SAO_JSON;
    } else {
        ESP_LOGI(TAG, "Identifying SAO (big EEPROM)...");
        dump_eeprom_contents(&sao_eeprom_big);
        esp_err_t result = eeprom_read(&sao_eeprom_big, 0, (uint8_t*) &header, sizeof(header));
        if (result != ESP_OK) {
            return ESP_OK;
        }
        if (memcmp(&header.magic[1], "IFE", 3) == 0) {
            ESP_LOGI(TAG, "SAO with binary descriptor on big EEPROM detected");
            return sao_identify_binary(sao, &sao_eeprom_big, &header);
        } else if (memcmp(&header.magic[1], "SON", 3) == 0) {
            // https://badge.a-combinator.com/addons/addon-id/
            ESP_LOGI(TAG, "SAO with JSON descriptor on big EEPROM detected");
            sao->type = SAO_JSON;
        } else {
            ESP_LOGI(TAG, "Unformatted SAO or SAO with unsupported formatting detected");
            sao->type = SAO_UNFORMATTED;
        }
    }
    return ESP_OK;
}

esp_err_t sao_format(const char* name, const char* driver, const uint8_t* driver_data, uint8_t driver_data_length, const char* driver2,
                     const uint8_t* driver2_data, uint8_t driver2_data_length, const char* driver3, const uint8_t* driver3_data, uint8_t driver3_data_length,
                     bool small) {
    uint8_t data[256];

    size_t position = 0;

    sao_binary_header_t* header = (sao_binary_header_t*) data;
    memcpy(header->magic, "LIFE", 4);
    header->name_length             = strlen(name);
    header->driver_name_length      = strlen(driver);
    header->driver_data_length      = driver_data_length;
    header->number_of_extra_drivers = 0;
    position += sizeof(sao_binary_header_t);

    memcpy(&data[position], name, strlen(name));
    position += strlen(name);

    memcpy(&data[position], driver, strlen(driver));
    position += strlen(driver);

    memcpy(&data[position], driver_data, driver_data_length);
    position += driver_data_length;

    if (driver2 != NULL) {
        sao_binary_extra_driver_t* extra_driver_header = (sao_binary_extra_driver_t*) &data[position];
        extra_driver_header->driver_name_length        = strlen(driver2);
        extra_driver_header->driver_data_length        = driver2_data_length;
        position += sizeof(sao_binary_extra_driver_t);
        memcpy(&data[position], driver2, strlen(driver2));
        position += strlen(driver2);
        memcpy(&data[position], driver2_data, driver2_data_length);
        position += driver2_data_length;
        header->number_of_extra_drivers++;
    }

    if (driver3 != NULL) {
        sao_binary_extra_driver_t* extra_driver_header = (sao_binary_extra_driver_t*) &data[position];
        extra_driver_header->driver_name_length        = strlen(driver3);
        extra_driver_header->driver_data_length        = driver3_data_length;
        position += sizeof(sao_binary_extra_driver_t);
        memcpy(&data[position], driver3, strlen(driver3));
        position += strlen(driver3);
        memcpy(&data[position], driver3_data, driver3_data_length);
        position += driver3_data_length;
        header->number_of_extra_drivers++;
    }

    if (small) {
        printf("Writing %u bytes to small EEPROM\n", position);
        return eeprom_write(&sao_eeprom_small, 0, data, position);
    } else {
        printf("Writing %u bytes to big EEPROM\n", position);
        return eeprom_write(&sao_eeprom_big, 0, data, position);
    }
}
