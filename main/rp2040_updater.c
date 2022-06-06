#include <stdio.h>
#include <string.h>
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_system.h>
#include <esp_err.h>
#include <esp_log.h>
#include "driver/uart.h"
#include "hardware.h"
#include "managed_i2c.h"
#include "pax_gfx.h"
#include "rp2040.h"
#include "rp2040bl.h"
#include "system_wrapper.h"
#include "graphics_wrapper.h"
#include "esp32/rom/crc.h"

extern const uint8_t rp2040_firmware_bin_start[] asm("_binary_rp2040_firmware_bin_start");
extern const uint8_t rp2040_firmware_bin_end[] asm("_binary_rp2040_firmware_bin_end");

void display_rp2040_update_state(pax_buf_t* pax_buffer, ILI9341* ili9341, const char* text) {
    pax_noclip(pax_buffer);
    const pax_font_t* font = pax_get_font("sky mono");
    pax_background(pax_buffer, 0xFFFFFF);
    pax_vec1_t title_size = pax_text_size(font, 18, "Co-processor update");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, (320 / 2) - (title_size.x / 2), 120 - 30, "Co-processor update");
    pax_vec1_t size = pax_text_size(font, 18, text);
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, (320 / 2) - (size.x / 2), 120 + 10, text);
    ili9341_write(ili9341, pax_buffer->buf);
}

void rp2040_updater(RP2040* rp2040, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    size_t firmware_size = rp2040_firmware_bin_end - rp2040_firmware_bin_start;
    char message[64];
    
    uint8_t fw_version;
    if (rp2040_get_firmware_version(rp2040, &fw_version) != ESP_OK) {
        pax_noclip(pax_buffer);
        pax_background(pax_buffer, 0xa85a32);
        snprintf(message, sizeof(message) - 1, "RP2040 error");
        pax_draw_text(pax_buffer, 0xFFFFFFFF, NULL, 18, 0, 20*0, message);
        snprintf(message, sizeof(message) - 1, "Failed to read firmware version");
        pax_draw_text(pax_buffer, 0xFFFFFFFF, NULL, 13, 0, 20*1, message);
        ili9341_write(ili9341, pax_buffer->buf);
        restart();
    }

    if (fw_version < 0x03) { // Update required
        display_rp2040_update_state(pax_buffer, ili9341, "Starting bootloader...");
        rp2040_reboot_to_bootloader(rp2040);
        esp_restart();
    }
    
    if (fw_version == 0xFF) { // RP2040 is in bootloader mode
        display_rp2040_update_state(pax_buffer, ili9341, "Starting update...");

        uint8_t bl_version;
        if (rp2040_get_bootloader_version(rp2040, &bl_version) != ESP_OK) {
            pax_noclip(pax_buffer);
            pax_background(pax_buffer, 0xa85a32);
            snprintf(message, sizeof(message) - 1, "RP2040 update failed");
            pax_draw_text(pax_buffer, 0xFFFFFFFF, NULL, 18, 0, 20*0, message);
            snprintf(message, sizeof(message) - 1, "Communication error (1)");
            pax_draw_text(pax_buffer, 0xFFFFFFFF, NULL, 13, 0, 20*1, message);
            ili9341_write(ili9341, pax_buffer->buf);
            restart();
        }
        if (bl_version != 0x01) {
            pax_background(pax_buffer, 0xa85a32);
            snprintf(message, sizeof(message) - 1, "RP2040 update failed");
            pax_draw_text(pax_buffer, 0xFFFFFFFF, NULL, 18, 0, 20*0, message);
            snprintf(message, sizeof(message) - 1, "Unsupported bootloader version");
            pax_draw_text(pax_buffer, 0xFFFFFFFF, NULL, 13, 0, 20*1, message);
            ili9341_write(ili9341, pax_buffer->buf);
            restart();
        }
        
        rp2040_bl_install_uart();
        
        display_rp2040_update_state(pax_buffer, ili9341, "Preparing...");

        while (true) {
            vTaskDelay(1 / portTICK_PERIOD_MS);
            uint8_t bl_state;
            if (rp2040_get_bootloader_state(rp2040, &bl_state) != ESP_OK) {
                pax_noclip(pax_buffer);
                pax_background(pax_buffer, 0xa85a32);
                snprintf(message, sizeof(message) - 1, "RP2040 update failed");
                pax_draw_text(pax_buffer, 0xFFFFFFFF, NULL, 18, 0, 20*0, message);
                snprintf(message, sizeof(message) - 1, "Communication error (2)");
                pax_draw_text(pax_buffer, 0xFFFFFFFF, NULL, 13, 0, 20*1, message);
                ili9341_write(ili9341, pax_buffer->buf);
                restart();
            }
            if (bl_state == 0xB0) {
                break;
            }
            if (bl_state > 0xB0) {
                pax_noclip(pax_buffer);
                pax_background(pax_buffer, 0xa85a32);
                snprintf(message, sizeof(message) - 1, "RP2040 update failed");
                pax_draw_text(pax_buffer, 0xFFFFFFFF, NULL, 18, 0, 20*0, message);
                snprintf(message, sizeof(message) - 1, "Unknown bootloader state");
                pax_draw_text(pax_buffer, 0xFFFFFFFF, NULL, 13, 0, 20*1, message);
                ili9341_write(ili9341, pax_buffer->buf);
                restart();
            }
        }

        display_rp2040_update_state(pax_buffer, ili9341, "Synchronizing...");

        while (true) {
            if (rp2040_bl_sync()) break;
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
        
        uint32_t flash_start = 0, flash_size = 0, erase_size = 0, write_size = 0, max_data_len = 0;
        
        bool success = rp2040_bl_get_info(&flash_start, &flash_size, &erase_size, &write_size, &max_data_len);
        
        if (!success) {
            pax_noclip(pax_buffer);
            pax_background(pax_buffer, 0xa85a32);
            snprintf(message, sizeof(message) - 1, "RP2040 update failed");
            pax_draw_text(pax_buffer, 0xFFFFFFFF, NULL, 18, 0, 20*0, message);
            snprintf(message, sizeof(message) - 1, "Failed to read information");
            pax_draw_text(pax_buffer, 0xFFFFFFFF, NULL, 13, 0, 20*1, message);
            ili9341_write(ili9341, pax_buffer->buf);
            restart();
        }
        
        display_rp2040_update_state(pax_buffer, ili9341, "Erasing...");
        
        uint32_t erase_length = firmware_size;
        erase_length = erase_length + erase_size - (erase_length % erase_size); // Round up to erase size
        
        if (erase_length > flash_size - erase_size) {
            erase_length = flash_size - erase_size;
        }
        
        bool eraseSuccess = rp2040_bl_erase(flash_start, flash_size - erase_size);//erase_length); < erase whole flash as workaround for a yet to be fixed bug in the calculation of erase_length
        
        if (!eraseSuccess) {
            pax_noclip(pax_buffer);
            pax_background(pax_buffer, 0xa85a32);
            snprintf(message, sizeof(message) - 1, "RP2040 update failed");
            pax_draw_text(pax_buffer, 0xFFFFFFFF, NULL, 18, 0, 20*0, message);
            snprintf(message, sizeof(message) - 1, "Failed to erase flash");
            pax_draw_text(pax_buffer, 0xFFFFFFFF, NULL, 13, 0, 20*1, message);
            ili9341_write(ili9341, pax_buffer->buf);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            restart();
        }
        
        uint32_t position = 0;
        uint32_t txSize = write_size;
        uint8_t* txBuffer = malloc(write_size);
        
        uint32_t blockCrc = 0;
        uint32_t totalCrc = 0;
        uint32_t totalLength = 0;
        
        while (true) {
            if ((firmware_size - position) < txSize) {
                txSize = firmware_size - position;
            }
            
            if (txSize == 0) break;

            uint8_t percentage = position * 100 / firmware_size;
            
            pax_noclip(pax_buffer);
            pax_background(pax_buffer, 0x325aa8);
            snprintf(message, sizeof(message) - 1, "Writing... %u%%", percentage);
            display_rp2040_update_state(pax_buffer, ili9341, message);

            uint32_t checkCrc = 0;
            memset(txBuffer, 0, write_size);
            memcpy(txBuffer, &rp2040_firmware_bin_start[position], txSize);
            blockCrc = crc32_le(0, txBuffer, write_size);
            totalCrc = crc32_le(totalCrc, txBuffer, write_size);
            totalLength += write_size;
            bool writeSuccess = rp2040_bl_write(0x10010000 + position, write_size, txBuffer, &checkCrc);
            if (writeSuccess && (blockCrc == checkCrc)) {
                position += txSize;
            } else {
                display_rp2040_update_state(pax_buffer, ili9341, "CRC mismatch");
                while (!rp2040_bl_sync()) {
                    vTaskDelay(20 / portTICK_PERIOD_MS);
                }
            }
        }
        
        free(txBuffer);
        
        display_rp2040_update_state(pax_buffer, ili9341, "Finalizing...");
        
        bool sealRes = rp2040_bl_seal(0x10010000, 0x10010000, totalLength, totalCrc);
        
        if (sealRes) {
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            pax_noclip(pax_buffer);
            pax_background(pax_buffer, 0xCCCCCC);
            memset(message, 0, sizeof(message));
            display_rp2040_update_state(pax_buffer, ili9341, "Update completed");
            rp2040_bl_go(0x10010000);
        } else {
            display_rp2040_update_state(pax_buffer, ili9341, "Update failed");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            restart();
        }

        while (true) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }
}
