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
#define RP2040_FIRMWARE_ADDR 0x10010000
#define RP2040_SECTOR_SIZE 0x1000

static void draw_text_centered(pax_buf_t* pax_buffer, const pax_font_t* font, pax_col_t color, int offset, const char* text) {
    pax_center_text(pax_buffer, color, font, 18, pax_buffer->width/2, pax_buffer->height/2+offset, text);
}

void display_rp2040_update_state(pax_buf_t* pax_buffer, ILI9341* ili9341, const char* text1, const char* text2) {
    pax_noclip(pax_buffer);
    const pax_font_t *font = pax_get_font("saira regular");
    pax_background(pax_buffer, 0xFFFFFF);
    draw_text_centered(pax_buffer, font, 0xFF000000, -16, "Co-processor update");
    draw_text_centered(pax_buffer, font, 0xFF000000, 8, text1);
    if (text2 != NULL) {
        draw_text_centered(pax_buffer, font, 0xFF000000, 32, text2);
    }
    ili9341_write(ili9341, pax_buffer->buf);
}

void display_rp2040_update_error(pax_buf_t* pax_buffer, ILI9341* ili9341, const char* text) {
    pax_noclip(pax_buffer);
    const pax_font_t *font = pax_get_font("saira regular");
    pax_background(pax_buffer, 0xa85a32);
    draw_text_centered(pax_buffer, font, 0xFFFFFFFF, -8, "ERROR");
    draw_text_centered(pax_buffer, font, 0xFFFFFFFF, 8, text);
    ili9341_write(ili9341, pax_buffer->buf);
}

void display_rp2040_update_old_bootloader(pax_buf_t* pax_buffer, ILI9341* ili9341) {
    pax_noclip(pax_buffer);
    const pax_font_t *font = pax_get_font("saira regular");
    pax_background(pax_buffer, 0xa85a32);

    int line_height = 16;

    draw_text_centered(pax_buffer, font, 0xFFFFFFFF, line_height*-7, "Hi there prototype user,");
    draw_text_centered(pax_buffer, font, 0xFFFFFFFF, line_height*-6, "please flash the new bootloader!");

    draw_text_centered(pax_buffer, font, 0xFFFFFFFF, line_height*-4, "You can do so by downloading");
    draw_text_centered(pax_buffer, font, 0xFFFFFFFF, line_height*-3, "the UF2 file at the site below.");

    draw_text_centered(pax_buffer, font, 0xFFFFFFFF, line_height*-1, "Hold SELECT while powering on");
    draw_text_centered(pax_buffer, font, 0xFFFFFFFF, 0, "your badge and copy the file to");
    draw_text_centered(pax_buffer, font, 0xFFFFFFFF, line_height*1, "the RPI-R2 disk that appears.");

    draw_text_centered(pax_buffer, font, 0xFFFFFFFF, line_height*3, "ota.bodge.team/mch2022.uf2");
    draw_text_centered(pax_buffer, font, 0xFFFFFFFF, line_height*4, "(that's bodge, with an o)");
    ili9341_write(ili9341, pax_buffer->buf);
}

void rp2040_updater(RP2040* rp2040, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    size_t firmware_size = rp2040_firmware_bin_end - rp2040_firmware_bin_start;
    
    uint8_t fw_version;
    if (rp2040_get_firmware_version(rp2040, &fw_version) != ESP_OK) {
        display_rp2040_update_error(pax_buffer, ili9341, "Failed to read firmware version");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        restart();
    }

    if (fw_version < 0x06) { // Update required
        display_rp2040_update_state(pax_buffer, ili9341, "Starting bootloader...", NULL);
        rp2040_reboot_to_bootloader(rp2040);
        esp_restart();
    }
    
    if (fw_version == 0xFF) { // RP2040 is in bootloader mode
        display_rp2040_update_state(pax_buffer, ili9341, "Starting update...", NULL);

        uint8_t bl_version;
        if (rp2040_get_bootloader_version(rp2040, &bl_version) != ESP_OK) {
            display_rp2040_update_error(pax_buffer, ili9341, "Failed to read bootloader version");
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            restart();
        }
        if (bl_version != 0x02) {
            display_rp2040_update_old_bootloader(pax_buffer, ili9341);
            while (true) {
                vTaskDelay(1000 / portTICK_PERIOD_MS);
            }
        }
        
        rp2040_bl_install_uart();
        
        display_rp2040_update_state(pax_buffer, ili9341, "Preparing...", NULL);

        while (true) {
            vTaskDelay(1 / portTICK_PERIOD_MS);
            uint8_t bl_state;
            if (rp2040_get_bootloader_state(rp2040, &bl_state) != ESP_OK) {
                display_rp2040_update_error(pax_buffer, ili9341, "Failed to read state");
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                restart();
            }
            if (bl_state == 0xB0) {
                break;
            }
            if (bl_state > 0xB0) {
                display_rp2040_update_error(pax_buffer, ili9341, "Unknown state");
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                restart();
            }
        }

        display_rp2040_update_state(pax_buffer, ili9341, "Synchronizing...", NULL);

        while (true) {
            if (rp2040_bl_sync()) break;
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
        
        uint32_t flash_start = 0, flash_size = 0, erase_size = 0, write_size = 0, max_data_len = 0;
        
        bool success = rp2040_bl_get_info(&flash_start, &flash_size, &erase_size, &write_size, &max_data_len);
        
        if (!success) {
            display_rp2040_update_error(pax_buffer, ili9341, "Failed to read information");
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            restart();
        }

        display_rp2040_update_state(pax_buffer, ili9341, "Erasing...", NULL);

        if (!rp2040_bl_erase(flash_start, RP2040_SECTOR_SIZE)) {
            display_rp2040_update_error(pax_buffer, ili9341, "Failed to erase header");
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            restart();
        }
        
        size_t firmware_size_erase = firmware_size + erase_size - (firmware_size % erase_size);
        if (!rp2040_bl_erase(RP2040_FIRMWARE_ADDR, firmware_size_erase)) {
            display_rp2040_update_error(pax_buffer, ili9341, "Flash erase failed");
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            restart();
        }
        
        uint32_t position = 0;
        uint32_t txSize = write_size;
        uint8_t* txBuffer = malloc(write_size);
        
        uint32_t blockCrc = 0;
        uint32_t totalCrc = 0;
        uint32_t totalLength = 0;
        
        uint8_t prev_percentage = 0;
        
        while (true) {
            if ((firmware_size - position) < txSize) {
                txSize = firmware_size - position;
            }
            
            if (txSize == 0) break;

            uint8_t percentage = position * 100 / firmware_size;
            if (percentage != prev_percentage) {
                prev_percentage = percentage;
                char percentage_str[64];
                snprintf(percentage_str, sizeof(percentage_str), "%u%%", percentage);
                display_rp2040_update_state(pax_buffer, ili9341, "Writing...", percentage_str);
            }

            uint32_t checkCrc = 0;
            memset(txBuffer, 0, write_size);
            memcpy(txBuffer, &rp2040_firmware_bin_start[position], txSize);
            blockCrc = crc32_le(0, txBuffer, write_size);
            totalCrc = crc32_le(totalCrc, txBuffer, write_size);
            totalLength += write_size;
            bool writeSuccess = rp2040_bl_write(RP2040_FIRMWARE_ADDR + position, write_size, txBuffer, &checkCrc);
            if (writeSuccess && (blockCrc == checkCrc)) {
                position += txSize;
            } else {
                display_rp2040_update_error(pax_buffer, ili9341, "CRC check failed");
                prev_percentage = 0;
                while (!rp2040_bl_sync()) {
                    vTaskDelay(20 / portTICK_PERIOD_MS);
                }
            }
        }
        
        free(txBuffer);
        
        display_rp2040_update_state(pax_buffer, ili9341, "Sealing...", NULL);
        
        bool sealRes = rp2040_bl_seal(RP2040_FIRMWARE_ADDR, totalLength, totalCrc);
        
        if (sealRes) {
            display_rp2040_update_state(pax_buffer, ili9341, "Update completed", NULL);
            rp2040_bl_go(RP2040_FIRMWARE_ADDR);
        } else {
            display_rp2040_update_error(pax_buffer, ili9341, "Sealing failed");
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            restart();
        }

        while (true) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }
}
