#include <stdio.h>
#include <string.h>
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_system.h>
#include <esp_err.h>
#include <esp_log.h>
#include "appfs.h"
#include "ili9341.h"
#include "pax_gfx.h"
#include "menu.h"
#include "rp2040.h"
#include "appfs_wrapper.h"
#include "hardware.h"
#include "system_wrapper.h"
#include "bootscreen.h"
#include "esp_sleep.h"
#include "soc/rtc.h"
#include "soc/rtc_cntl_reg.h"

static const char *TAG = "appfs wrapper";

esp_err_t appfs_init(void) {
    return appfsInit(APPFS_PART_TYPE, APPFS_PART_SUBTYPE);
}

uint8_t* load_file_to_ram(FILE* fd, size_t* fsize) {
    fseek(fd, 0, SEEK_END);
    *fsize = ftell(fd);
    fseek(fd, 0, SEEK_SET);
    uint8_t* file = malloc(*fsize);
    if (file == NULL) return NULL;
    fread(file, *fsize, 1, fd);
    return file;
}

void appfs_boot_app(int fd) {
    if (fd<0 || fd>255) {
        REG_WRITE(RTC_CNTL_STORE0_REG, 0);
    } else {
        REG_WRITE(RTC_CNTL_STORE0_REG, 0xA5000000|fd);
    }
    
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
    esp_sleep_enable_timer_wakeup(10);
    esp_deep_sleep_start();
}

void appfs_store_app(pax_buf_t* pax_buffer, ILI9341* ili9341, char* path, char* label) {
    display_boot_screen(pax_buffer, ili9341, "Installing app...");
    esp_err_t res;
    appfs_handle_t handle;
    FILE* app_fd = fopen(path, "rb");
    if (app_fd == NULL) {
        display_boot_screen(pax_buffer, ili9341, "Failed to open file");
        ESP_LOGE(TAG, "Failed to open file");
        vTaskDelay(100 / portTICK_PERIOD_MS);
        return;
    }
    size_t app_size;
    uint8_t* app = load_file_to_ram(app_fd, &app_size);
    if (app == NULL) {
        display_boot_screen(pax_buffer, ili9341, "Failed to load app to RAM");
        ESP_LOGE(TAG, "Failed to load application into RAM");
        vTaskDelay(100 / portTICK_PERIOD_MS);
        return;
    }

    ESP_LOGI(TAG, "Application size %d", app_size);

    res = appfsCreateFile(label, app_size, &handle);
    if (res != ESP_OK) {
        display_boot_screen(pax_buffer, ili9341, "Failed to create on AppFS");
        ESP_LOGE(TAG, "Failed to create file on AppFS (%d)", res);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        free(app);
        return;
    }
    res = appfsWrite(handle, 0, app, app_size);
    if (res != ESP_OK) {
        display_boot_screen(pax_buffer, ili9341, "Failed to write to AppFS");
        ESP_LOGE(TAG, "Failed to write to file on AppFS (%d)", res);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        free(app);
        return;
    }
    free(app);
    ESP_LOGI(TAG, "Application is now stored in AppFS");
    display_boot_screen(pax_buffer, ili9341, "App installed!");
    vTaskDelay(100 / portTICK_PERIOD_MS);
    return;
}
