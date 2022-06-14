#include "packetutils.h"
#include <string.h>
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/rtc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_sleep.h"
#include "fsob_backend.h"
#include "esp_spi_flash.h"

#define TAG "fsob_appfs"

/**
 * @brief Redefine the appfs functions used. This allows to compile the component when appfs support is disabled.
 * 
 */
#define APPFS_INVALID_FD (-1)
typedef int appfs_handle_t;
void appfsEntryInfo(appfs_handle_t fd, const char **name, int *size);
appfs_handle_t appfsNextEntry(appfs_handle_t fd);
esp_err_t appfsDeleteFile(const char *filename);
esp_err_t appfsCreateFile(const char *filename, size_t size, appfs_handle_t *handle);
esp_err_t appfsErase(appfs_handle_t fd, size_t start, size_t len);
esp_err_t appfsWrite(appfs_handle_t fd, size_t start, uint8_t *buf, size_t len);
appfs_handle_t appfsOpen(const char *filename);

int appfslist(uint8_t *data, uint16_t command, uint32_t message_id, uint32_t size, uint32_t received, uint32_t length) {
    if(received != size) return 0;

    uint32_t amount_of_files = 0;
    uint32_t buffer_size = 0;
    appfs_handle_t appfs_fd = APPFS_INVALID_FD;
    while (1) {
        appfs_fd = appfsNextEntry(appfs_fd);
        if (appfs_fd == APPFS_INVALID_FD) break;
        const char* name;
        int app_size;
        appfsEntryInfo(appfs_fd, &name, &app_size);
        amount_of_files++;
        buffer_size += strlen(name);
    }
    int payloadlength = 4 + buffer_size + amount_of_files*(4+4); //amount of files + all string length + for every entry app size + app name length
    
    uint8_t header[12];    
    createMessageHeader(header, command, payloadlength, message_id);
    fsob_write_bytes((const char*) header, 12);
    fsob_write_bytes((char *) &amount_of_files, 4);
    
    appfs_fd = APPFS_INVALID_FD;
    while (1) {
        appfs_fd = appfsNextEntry(appfs_fd);
        if (appfs_fd == APPFS_INVALID_FD) break;
        const char* name;
        int app_size;
        appfsEntryInfo(appfs_fd, &name, &app_size);
        fsob_write_bytes((char *) &app_size, 4);
        uint32_t name_length = strlen(name);
        fsob_write_bytes((char *) &name_length, 4);
        fsob_write_bytes(name, name_length);
    }
    return 1;
}

int appfsdel(uint8_t *data, uint16_t command, uint32_t message_id, uint32_t size, uint32_t received, uint32_t length) {
    if(received != size) return 0;
    esp_err_t res = appfsDeleteFile((char *) data);
    if (res == ESP_OK) {
        sendok(command, message_id);
    } else {
        sender(command, message_id);
    }
    return 1;
}

int appfswrite(uint8_t *data, uint16_t command, uint32_t message_id, uint32_t size, uint32_t received, uint32_t length) {
    static appfs_handle_t handle = APPFS_INVALID_FD;
    static bool failed_open = false;
    static int app_size = 0;
    static int written = 0;
    
    if(received == length) {    //Opening new file, cleaning up statics just in case
        failed_open = false;
        handle = APPFS_INVALID_FD;
        app_size = 0;
        written = 0;
    }

    if(handle == APPFS_INVALID_FD && failed_open == false) {
        for(int i = 0; i < received; i++) {
            if(data[i] == 0) {
                app_size = size-i;
                esp_err_t res = appfsCreateFile((char *) data, app_size, &handle);
                if (res != ESP_OK) {
                    failed_open = true;
                    return 1;
                }

                int roundedSize=(app_size+(SPI_FLASH_MMU_PAGE_SIZE-1))&(~(SPI_FLASH_MMU_PAGE_SIZE-1));
                ESP_LOGI(TAG, "Erasing flash");
                res = appfsErase(handle, 0, roundedSize);

                if(length > i) {
                    appfsWrite(handle, 0, &data[i+1], length-i);
                    written = length - i;
                }

                if(received == size) {    //Creating an empty file or short. Close the file and send reply
                    failed_open = 0;
                    if(handle != APPFS_INVALID_FD) {
                        sendok(command, message_id);
                        handle = APPFS_INVALID_FD;
                    } else {
                        sender(command, message_id);
                    }
                }
                return 1;
            }
        }
    } else if(handle != APPFS_INVALID_FD && failed_open == false) {
        appfsWrite(handle, written, data, length);
        written += length;
    }

    if(received == size) {    //Creating an empty file or short. Close the file and send reply
        failed_open = 0;
        if(handle != APPFS_INVALID_FD) {
            sendok(command, message_id);
            handle = APPFS_INVALID_FD;
        } else {
            sender(command, message_id);
        }
    }
    return 0;
}

int appfsboot(uint8_t *data, uint16_t command, uint32_t message_id, uint32_t size, uint32_t received, uint32_t length) {
    if(received != size) return 0;
    appfs_handle_t fd = appfsOpen((char *) data);
    if (fd == APPFS_INVALID_FD) {
        sender(command, message_id);
        return 1;
    }
    sendok(command, message_id);
    vTaskDelay(100 / portTICK_PERIOD_MS);
     if (fd<0 || fd>255) {
        REG_WRITE(RTC_CNTL_STORE0_REG, 0);
    } else {
        REG_WRITE(RTC_CNTL_STORE0_REG, 0xA5000000|fd);
    }
    
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
    esp_sleep_enable_timer_wakeup(10);
    esp_deep_sleep_start();
    return 1;
}