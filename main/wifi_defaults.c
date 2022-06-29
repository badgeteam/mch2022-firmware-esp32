#include "wifi_defaults.h"

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

static const char *TAG = "wifi_defaults";

bool wifi_set_defaults() {
    nvs_handle_t handle;
    esp_err_t    res = nvs_open("system", NVS_READWRITE, &handle);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Can't set WiFi to default: %s", esp_err_to_name(res));
        return false;
    }
    nvs_set_u8(handle, "wifi.authmode", WIFI_MCH2022_AUTH);
    nvs_set_u8(handle, "wifi.phase2", WIFI_MCH2022_PHASE2);
    nvs_set_str(handle, "wifi.ssid", WIFI_MCH2022_SSID);
    nvs_set_str(handle, "wifi.username", WIFI_MCH2022_USER);
    nvs_set_str(handle, "wifi.anon_ident", WIFI_MCH2022_IDENT);
    nvs_set_str(handle, "wifi.password", WIFI_MCH2022_PASSWORD);

    res = nvs_commit(handle);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Can't set WiFi to default: %s", esp_err_to_name(res));
        nvs_close(handle);
        return false;
    }
    nvs_close(handle);
    return true;
}

bool wifi_check_configured() {
    nvs_handle_t handle;
    esp_err_t    res = nvs_open("system", NVS_READWRITE, &handle);
    if (res != ESP_OK) {
        nvs_close(handle);
        return false;
    }

    size_t len;
    res = nvs_get_str(handle, "wifi.ssid", NULL, &len);
    if ((res != ESP_OK) || (len < 1)) {
        nvs_close(handle);
        return false;
    }

    nvs_close(handle);
    return true;
}
