#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "ili9341.h"
#include "ice40.h"
#include "hardware.h"
#include "button_wrapper.h"

static const char *TAG = "settings";

esp_err_t nvs_init() {
    const esp_partition_t * nvs_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
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
