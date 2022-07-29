#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <string.h>

#include "hardware.h"

esp_err_t nvs_init() {
    const esp_partition_t* nvs_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
    if (nvs_partition == NULL) return ESP_FAIL;
    esp_err_t res = nvs_flash_init();
    if (res != ESP_OK) {
        res = esp_partition_erase_range(nvs_partition, 0, nvs_partition->size);
        if (res != ESP_OK) return res;
        res = nvs_flash_init();
        if (res != ESP_OK) return res;
    }
    return ESP_OK;
}

esp_err_t nvs_get_str_fixed(const char* nvs_namespace, const char* key, char* target, size_t target_size, size_t* size) {
    nvs_handle_t handle;
    esp_err_t    res;

    res = nvs_open(nvs_namespace, NVS_READWRITE, &handle);
    if (res != ESP_OK) return res;

    size_t required_size;
    res = nvs_get_str(handle, key, NULL, &required_size);
    if (res != ESP_OK) {
        nvs_close(handle);
        return res;
    }

    if (required_size > target_size) {
        nvs_close(handle);
        return ESP_FAIL;
    }

    res = nvs_get_str(handle, key, target, &required_size);

    if (size != NULL) *size = required_size;

    nvs_close(handle);

    return res;
}

uint8_t nvs_get_u8_default(const char* nvs_namespace, const char* key, uint8_t default_value) {
    nvs_handle_t handle;
    esp_err_t    res;

    res = nvs_open(nvs_namespace, NVS_READWRITE, &handle);
    if (res != ESP_OK) return default_value;

    uint8_t target;
    res = nvs_get_u8(handle, key, &target);
    if (res != ESP_OK) {
        nvs_close(handle);
        return default_value;
    }

    nvs_close(handle);
    return target;
}

esp_err_t nvs_set_u8_fixed(const char* nvs_namespace, const char* key, uint8_t value) {
    nvs_handle_t handle;
    esp_err_t    res;

    res = nvs_open(nvs_namespace, NVS_READWRITE, &handle);
    if (res != ESP_OK) return res;

    res = nvs_set_u8(handle, key, value);
    nvs_close(handle);

    return res;
}
