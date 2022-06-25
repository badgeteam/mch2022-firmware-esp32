#include "rtc_memory.h"

#include <esp32/ulp.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <rom/crc.h>
#include <stdio.h>
#include <string.h>

#define RTC_MEM_INT_SIZE 64
#define RTC_MEM_STR_SIZE 512

static int *const      rtc_mem_int     = (int *const) (RTC_SLOW_MEM + CONFIG_ESP32_ULP_COPROC_RESERVE_MEM);
static uint16_t *const rtc_mem_int_crc = (uint16_t *const) (rtc_mem_int + (sizeof(int) * RTC_MEM_INT_SIZE));
static char *const     rtc_mem_str     = (char *const) (rtc_mem_int_crc + sizeof(uint16_t));
static uint16_t *const rtc_mem_str_crc = (uint16_t *const) (rtc_mem_str + (RTC_MEM_STR_SIZE * sizeof(char)));

esp_err_t rtc_memory_int_write(int pos, int val) {
    if (pos >= RTC_MEM_INT_SIZE) return ESP_FAIL;
    rtc_mem_int[pos] = val;
    *rtc_mem_int_crc = crc16_le(0, (uint8_t const *) rtc_mem_int, RTC_MEM_INT_SIZE * sizeof(int));
    return ESP_OK;
}

esp_err_t rtc_memory_int_read(int pos, int *val) {
    if (pos >= RTC_MEM_INT_SIZE) return ESP_FAIL;
    if (*rtc_mem_int_crc != crc16_le(0, (uint8_t const *) rtc_mem_int, RTC_MEM_INT_SIZE * sizeof(int))) return ESP_FAIL;
    *val = rtc_mem_int[pos];
    return ESP_OK;
}

esp_err_t rtc_memory_string_write(const char *str) {
    if (strlen(str) >= RTC_MEM_STR_SIZE) return ESP_FAIL;
    memset(rtc_mem_str, 0, RTC_MEM_STR_SIZE * sizeof(char));
    strcpy(rtc_mem_str, str);
    *rtc_mem_str_crc = crc16_le(0, (uint8_t const *) rtc_mem_str, RTC_MEM_STR_SIZE);
    printf("RTC memory @ %p written, CRC %04X\n", rtc_mem_str, *rtc_mem_str_crc);
    return ESP_OK;
}

esp_err_t rtc_memory_string_read(const char **str) {
    printf("RTC memory @ %p read\n", rtc_mem_str);
    uint16_t crc = crc16_le(0, (uint8_t const *) rtc_mem_str, RTC_MEM_STR_SIZE);
    if (*rtc_mem_str_crc != crc) {
        printf("RTC memory @ %p invalid %04X != %04x\n", rtc_mem_str, *rtc_mem_str_crc, crc);
        return ESP_FAIL;
    }
    printf("RTC memory @ %p valid: %s\n", rtc_mem_str, rtc_mem_str);
    *str = rtc_mem_str;
    return ESP_OK;
}

esp_err_t rtc_memory_clear() {
    memset(rtc_mem_int, 0, RTC_MEM_INT_SIZE * sizeof(int));
    memset(rtc_mem_str, 0, RTC_MEM_STR_SIZE * sizeof(char));
    *rtc_mem_int_crc = 0;
    *rtc_mem_str_crc = 0;
    return ESP_OK;
}
