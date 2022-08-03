#include "appfs_wrapper.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "appfs.h"
#include "bootscreen.h"
#include "esp_sleep.h"
#include "graphics_wrapper.h"
#include "hardware.h"
#include "menu.h"
#include "pax_gfx.h"
#include "rp2040.h"
#include "soc/rtc.h"
#include "soc/rtc_cntl_reg.h"
#include "system_wrapper.h"

static const char* TAG = "appfs wrapper";

esp_err_t appfs_init(void) { return appfsInit(APPFS_PART_TYPE, APPFS_PART_SUBTYPE); }

appfs_handle_t appfs_detect_crash() {
    uint32_t r = REG_READ(RTC_CNTL_STORE0_REG);
    ESP_LOGI(TAG, "RTC store0 reg: %x", r);
    if ((r & 0xFF000000) != 0xA6000000) return APPFS_INVALID_FD;
    return r & 0xff;
}

void appfs_boot_app(int fd) {
    if (fd < 0 || fd > 255) {
        REG_WRITE(RTC_CNTL_STORE0_REG, 0);
    } else {
        REG_WRITE(RTC_CNTL_STORE0_REG, 0xA5000000 | fd);
    }

    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
    esp_sleep_enable_timer_wakeup(10);
    esp_deep_sleep_start();
}

void appfs_store_app(xQueueHandle button_queue, const char* path, const char* name, const char* title, uint16_t version) {
    display_boot_screen("Installing app...");
    esp_err_t res;
    FILE*     app_fd = fopen(path, "rb");
    if (app_fd == NULL) {
        render_message("Failed to open file");
        display_flush();
        ESP_LOGE(TAG, "Failed to open file");
        if (button_queue != NULL) wait_for_button();
        return;
    }
    size_t   app_size = get_file_size(app_fd);
    uint8_t* app      = load_file_to_ram(app_fd);
    fclose(app_fd);
    if (app == NULL) {
        render_message("Failed to load app to RAM");
        display_flush();
        ESP_LOGE(TAG, "Failed to load application into RAM");
        if (button_queue != NULL) wait_for_button();
        return;
    }

    ESP_LOGI(TAG, "Application size %d", app_size);

    res = appfs_store_in_memory_app(button_queue, name, title, version, app_size, app);
    if (res == ESP_OK) {
        render_message("App installed!");
        display_flush();
        if (button_queue != NULL) wait_for_button();
    }

    free(app);
}

esp_err_t appfs_store_in_memory_app(xQueueHandle button_queue, const char* name, const char* title, uint16_t version, size_t app_size, uint8_t* app) {
    appfs_handle_t handle;
    esp_err_t      res = appfsCreateFileExt(name, title, version, app_size, &handle);
    if (res != ESP_OK) {
        render_message("Failed to create file");
        display_flush();
        ESP_LOGE(TAG, "Failed to create file on AppFS (%d)", res);
        if (button_queue != NULL) wait_for_button();
        return res;
    }
    int roundedSize = (app_size + (SPI_FLASH_MMU_PAGE_SIZE - 1)) & (~(SPI_FLASH_MMU_PAGE_SIZE - 1));
    res             = appfsErase(handle, 0, roundedSize);
    if (res != ESP_OK) {
        render_message("Failed to erase file");
        display_flush();
        ESP_LOGE(TAG, "Failed to erase file on AppFS (%d)", res);
        if (button_queue != NULL) wait_for_button();
        return res;
    }
    res = appfsWrite(handle, 0, app, app_size);
    if (res != ESP_OK) {
        render_message("Failed to write file");
        display_flush();
        ESP_LOGE(TAG, "Failed to write to file on AppFS (%d)", res);
        if (button_queue != NULL) wait_for_button();
        return res;
    }
    ESP_LOGI(TAG, "Application is now stored in AppFS");
    return res;
}
