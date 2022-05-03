#include "appfs_wrapper.h"
#include "esp_sleep.h"
#include "soc/rtc.h"
#include "soc/rtc_cntl_reg.h"

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

