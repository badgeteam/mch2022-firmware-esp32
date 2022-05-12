#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_efuse_custom_table.h"

static const char *TAG = "efuse";

void halt(const char* reason) {
    ESP_LOGE(TAG, "Failed efuse write operation: %s", reason);
    while (true) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void efuse_protect() {
    // XPD settings
    if (esp_efuse_write_field_bit(ESP_EFUSE_XPD_SDIO_REG) != ESP_OK) halt("XPD_SDIO_REG"); // Enable the VDD_SDIO voltage regulator
    if (esp_efuse_write_field_bit(ESP_EFUSE_SDIO_TIEH)    != ESP_OK) halt("SDIO_TIEH");    // Set VDD_SDIO voltage regulator output to 3.3v
    if (esp_efuse_write_field_bit(ESP_EFUSE_SDIO_FORCE)   != ESP_OK) halt("SDIO_FORCE");   // Enable VDD_SDIO efuse override
    
    // Debug settings
    if (esp_efuse_write_field_bit(ESP_EFUSE_CONSOLE_DEBUG_DISABLE) != ESP_OK) halt("CONSOLE_DEBUG_DISABLE"); // Disable BASIC ROM console

    // Write protect
    if (esp_efuse_write_field_bit(ESP_EFUSE_WR_DIS_FLASH_CRYPT_CNT)                    != ESP_OK) halt("WR_DIS_FLASH_CRYPT_CNT");                    // Prevent disabling UART download mode
    if (esp_efuse_write_field_bit(ESP_EFUSE_WR_DIS_MAC_AND_CHIP_INFO)                  != ESP_OK) halt("WR_DIS_MAC_AND_CHIP_INFO");                  // Prevent writing to MAC address fuses
    if (esp_efuse_write_field_bit(ESP_EFUSE_WR_DIS_XPD)                                != ESP_OK) halt("WR_DIS_XPD");                                // Write protect XPD settings
    if (esp_efuse_write_field_bit(ESP_EFUSE_WR_DIS_SPI_PAD)                            != ESP_OK) halt("WR_DIS_SPI_PAD");                            // Pin mapping for SPI flash and PSRAM
    if (esp_efuse_write_field_bit(ESP_EFUSE_WR_DIS_SCHEME_KEY_CRYPT)                   != ESP_OK) halt("WR_DIS_SCHEME_KEY_CRYPT");                   // Disable flash encryption
    if (esp_efuse_write_field_bit(ESP_EFUSE_WR_DIS_ABS_DONE_0)                         != ESP_OK) halt("WR_DIS_ABS_DONE_0");                         // Disable secure boot V1
    if (esp_efuse_write_field_bit(ESP_EFUSE_WR_DIS_ABS_DONE_1)                         != ESP_OK) halt("WR_DIS_ABS_DONE_1");                         // Disable secure boot V2
    if (esp_efuse_write_field_bit(ESP_EFUSE_WR_DIS_CONSOLE_DEBUG_AND_DISABLE_DL_CRYPT) != ESP_OK) halt("WR_DIS_CONSOLE_DEBUG_AND_DISABLE_DL_CRYPT"); // Write protect download mode functions
    if (esp_efuse_write_field_bit(ESP_EFUSE_WR_DIS_BLK3)                               != ESP_OK) halt("WR_DIS_BLK3");                               // Write protect block 3 (to prevent changing MAC version)
    if (esp_efuse_write_field_bit(ESP_EFUSE_WR_DIS_EFUSE_RD_DISABLE)                   != ESP_OK) halt("WR_DIS_EFUSE_RD_DISABLE");                   // Write protect read disable and ADC vref
}

void efuse_print_state() {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    uint8_t mac_address[6];
    esp_err_t res = esp_efuse_mac_get_default(mac_address);
    if (res != ESP_OK) {
        printf("Error while reading MAC address: %d\n", res);
        return;
    }
    bool secure_boot_v1_enabled    = esp_efuse_read_field_bit(ESP_EFUSE_ABS_DONE_0);
    bool secure_boot_v2_enabled    = esp_efuse_read_field_bit(ESP_EFUSE_ABS_DONE_1);
    bool jtag_disabled             = esp_efuse_read_field_bit(ESP_EFUSE_DISABLE_JTAG);
    bool rom_basic_disabled        = esp_efuse_read_field_bit(ESP_EFUSE_CONSOLE_DEBUG_DISABLE);
    bool uart_download_disabled    = esp_efuse_read_field_bit(ESP_EFUSE_UART_DOWNLOAD_DIS);
    bool wp_efuse_rd_disable       = esp_efuse_read_field_bit(ESP_EFUSE_WR_DIS_EFUSE_RD_DISABLE);
    bool wp_wr_dis                 = esp_efuse_read_field_bit(ESP_EFUSE_WR_DIS_WR_DIS);
    bool wp_flash_crypt_cnt        = esp_efuse_read_field_bit(ESP_EFUSE_WR_DIS_FLASH_CRYPT_CNT);
    bool wp_mac_chip_info          = esp_efuse_read_field_bit(ESP_EFUSE_WR_DIS_MAC_AND_CHIP_INFO);
    bool wp_xpd                    = esp_efuse_read_field_bit(ESP_EFUSE_WR_DIS_XPD);
    bool wp_spi_pad                = esp_efuse_read_field_bit(ESP_EFUSE_WR_DIS_SPI_PAD);
    bool wp_blk1                   = esp_efuse_read_field_bit(ESP_EFUSE_WR_DIS_BLK1);
    bool wp_blk2                   = esp_efuse_read_field_bit(ESP_EFUSE_WR_DIS_BLK2);
    bool wp_blk3                   = esp_efuse_read_field_bit(ESP_EFUSE_WR_DIS_BLK3);
    bool wp_scheme_key_crypt       = esp_efuse_read_field_bit(ESP_EFUSE_WR_DIS_SCHEME_KEY_CRYPT);
    bool wp_abs_done_0             = esp_efuse_read_field_bit(ESP_EFUSE_WR_DIS_ABS_DONE_0);
    bool wp_abs_done_1             = esp_efuse_read_field_bit(ESP_EFUSE_WR_DIS_ABS_DONE_1);
    bool wp_jtag_disable           = esp_efuse_read_field_bit(ESP_EFUSE_WR_DIS_JTAG_DISABLE);
    bool wp_basic_console_dl_crypt = esp_efuse_read_field_bit(ESP_EFUSE_WR_DIS_CONSOLE_DEBUG_AND_DISABLE_DL_CRYPT);
    bool rd_blk1                   = esp_efuse_read_field_bit(ESP_EFUSE_RD_DIS_BLK1);
    bool rd_blk2                   = esp_efuse_read_field_bit(ESP_EFUSE_RD_DIS_BLK2);
    bool rd_blk3                   = esp_efuse_read_field_bit(ESP_EFUSE_RD_DIS_BLK3);
    printf("This is %s chip with %d CPU core(s), WiFi%s%s, ",
            CONFIG_IDF_TARGET,
            chip_info.cores,
            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
    printf("silicon revision %d, ", chip_info.revision);
    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024), (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    printf("Minimum free heap size: %d bytes\n", esp_get_minimum_free_heap_size());
    printf("MAC address:                                                 %02x:%02x:%02x:%02x:%02x:%02x\n", mac_address[0], mac_address[1], mac_address[2], mac_address[3], mac_address[4], mac_address[5]);
    printf("Secure boot v1 enabled:                                      %s\n", secure_boot_v1_enabled ? "yes" : "no");
    printf("Secure boot v2 enabled:                                      %s\n", secure_boot_v2_enabled ? "yes" : "no");
    printf("JTAG disabled:                                               %s\n", jtag_disabled ? "yes" : "no");
    printf("ROM BASIC disabled:                                          %s\n", rom_basic_disabled ? "yes" : "no");
    printf("UART DL mode disabled:                                       %s\n", uart_download_disabled ? "yes" : "no");
    printf("Write protect for EFUSE READ DISABLE                         %s\n", wp_efuse_rd_disable ? "yes" : "no");
    printf("Write protect for EFUSE WRITE DISABLE                        %s\n", wp_wr_dis ? "yes" : "no");
    printf("Write protect for FLASH_CRYPT_CNT:                           %s\n", wp_flash_crypt_cnt ? "yes" : "no");
    printf("Write protect for MAC & chip info:                           %s\n", wp_mac_chip_info ? "yes" : "no");
    printf("Write protect XPD settings:                                  %s\n", wp_xpd ? "yes" : "no");
    printf("Write protect SPI settings:                                  %s\n", wp_spi_pad ? "yes" : "no");
    printf("Write protect for EFUSE block 1:                             %s\n", wp_blk1 ? "yes" : "no");
    printf("Write protect for EFUSE block 2:                             %s\n", wp_blk2 ? "yes" : "no");
    printf("Write protect for EFUSE block 3:                             %s\n", wp_blk3 ? "yes" : "no");
    printf("Write protect for coding scheme, key status & crypto config: %s\n", wp_scheme_key_crypt ? "yes" : "no");
    printf("Write protect for secure boot V1 enable:                     %s\n", wp_abs_done_0 ? "yes" : "no");
    printf("Write protect for secure boot V2 enable:                     %s\n", wp_abs_done_1 ? "yes" : "no");
    printf("Write protect for JTAG disable:                              %s\n", wp_jtag_disable ? "yes" : "no");
    printf("Write protect for BASIC console disable & DL crypt:          %s\n", wp_basic_console_dl_crypt ? "yes" : "no");
    printf("Read disabled for EFUSE block 1:                             %s\n", rd_blk1 ? "yes" : "no");
    printf("Read disabled for EFUSE block 2:                             %s\n", rd_blk2 ? "yes" : "no");
    printf("Read disabled for EFUSE block 3:                             %s\n", rd_blk3 ? "yes" : "no");
    fflush(stdout);
}
