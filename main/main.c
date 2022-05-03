#include <stdio.h>
#include <string.h>
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_system.h>
#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "driver/uart.h"
#include "hardware.h"
#include "managed_i2c.h"
#include "pax_gfx.h"
#include "sdcard.h"
#include "appfs.h"
#include "driver_framebuffer.h"

#include "rp2040.h"

#include "fpga_test.h"

#include "menu.h"
#include "system_wrapper.h"
#include "graphics_wrapper.h"
#include "appfs_wrapper.h"
#include "settings.h"
#include "wifi_connection.h"

#include "ws2812.h"

#include "rp2040firmware.h"

static const char *TAG = "main";

typedef enum action {
    ACTION_NONE,
    ACTION_APPFS,
    ACTION_INSTALLER,
    ACTION_SETTINGS,
    ACTION_OTA,
    ACTION_FPGA,
    ACTION_RP2040_BL,
    ACTION_WIFI_CONNECT,
    ACTION_WIFI_SCAN,
    ACTION_WIFI_MANUAL,
    ACTION_WIFI_LIST,
    ACTION_BACK
} menu_action_t;

typedef struct _menu_args {
    appfs_handle_t fd;
    menu_action_t action;
} menu_args_t;

void appfs_store_app(pax_buf_t* pax_buffer, ILI9341* ili9341, uint8_t* framebuffer) {
    graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Installing app...");
    esp_err_t res;
    appfs_handle_t handle;
    FILE* app_fd = fopen("/sd/gnuboy.bin", "rb");
    if (app_fd == NULL) {
        graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Failed to open gnuboy.bin");
        ESP_LOGE(TAG, "Failed to open gnuboy.bin");
        vTaskDelay(100 / portTICK_PERIOD_MS);
        return;
    }
    size_t app_size;
    uint8_t* app = load_file_to_ram(app_fd, &app_size);
    if (app == NULL) {
        graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Failed to load app to RAM");
        ESP_LOGE(TAG, "Failed to load application into RAM");
        vTaskDelay(100 / portTICK_PERIOD_MS);
        return;
    }
    
    ESP_LOGI(TAG, "Application size %d", app_size);
    
    res = appfsCreateFile("gnuboy", app_size, &handle);
    if (res != ESP_OK) {
        graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Failed to create on AppFS");
        ESP_LOGE(TAG, "Failed to create file on AppFS (%d)", res);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        free(app);
        return;
    }
    res = appfsWrite(handle, 0, app, app_size);
    if (res != ESP_OK) {
        graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Failed to write to AppFS");
        ESP_LOGE(TAG, "Failed to write to file on AppFS (%d)", res);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        free(app);
        return;
    }
    free(app);
    ESP_LOGI(TAG, "Application is now stored in AppFS");
    graphics_task(pax_buffer, ili9341, framebuffer, NULL, "App installed!");
    vTaskDelay(100 / portTICK_PERIOD_MS);
    return;
}

void menu_launcher(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, uint8_t* framebuffer, menu_action_t* menu_action, appfs_handle_t* appfs_fd) {
    menu_t* menu = menu_alloc("Main menu");
    *appfs_fd = APPFS_INVALID_FD;
    *menu_action = ACTION_NONE;
    
    while (1) {
        *appfs_fd = appfsNextEntry(*appfs_fd);
        if (*appfs_fd == APPFS_INVALID_FD) break;
        const char* name = NULL;
        appfsEntryInfo(*appfs_fd, &name, NULL);
        menu_args_t* args = malloc(sizeof(menu_args_t));
        args->fd = *appfs_fd;
        args->action = ACTION_APPFS;
        menu_insert_item(menu, name, NULL, (void*) args, -1);
    }
    *appfs_fd = APPFS_INVALID_FD;

    menu_args_t* install_args = malloc(sizeof(menu_args_t));
    install_args->action = ACTION_INSTALLER;
    menu_insert_item(menu, "Hatchery", NULL, install_args, -1);
    
    menu_args_t* settings_args = malloc(sizeof(menu_args_t));
    settings_args->action = ACTION_SETTINGS;
    menu_insert_item(menu, "WiFi settings", NULL, settings_args, -1);
    
    menu_args_t* ota_args = malloc(sizeof(menu_args_t));
    ota_args->action = ACTION_OTA;
    menu_insert_item(menu, "Firmware update", NULL, ota_args, -1);

    menu_args_t* fpga_args = malloc(sizeof(menu_args_t));
    fpga_args->action = ACTION_FPGA;
    menu_insert_item(menu, "FPGA test", NULL, fpga_args, -1);
    
    menu_args_t* rp2040bl_args = malloc(sizeof(menu_args_t));
    rp2040bl_args->action = ACTION_RP2040_BL;
    menu_insert_item(menu, "RP2040 bootloader", NULL, rp2040bl_args, -1);
    
    menu_args_t* wifi_connect_args = malloc(sizeof(menu_args_t));
    wifi_connect_args->action = ACTION_WIFI_CONNECT;
    menu_insert_item(menu, "WiFi connect", NULL, wifi_connect_args, -1);

    bool render = true;
    menu_args_t* menuArgs = NULL;

    while (1) {
        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(buttonQueue, &buttonMessage, 16 / portTICK_PERIOD_MS) == pdTRUE) {
            uint8_t pin = buttonMessage.input;
            bool value = buttonMessage.state;
            switch(pin) {
                case RP2040_INPUT_JOYSTICK_DOWN:
                    if (value) {
                        menu_navigate_next(menu);
                        render = true;
                    }
                    break;
                case RP2040_INPUT_JOYSTICK_UP:
                    if (value) {
                        menu_navigate_previous(menu);
                        render = true;
                    }
                    break;
                case RP2040_INPUT_BUTTON_ACCEPT:
                    if (value) {
                        menuArgs = menu_get_callback_args(menu, menu_get_position(menu));
                    }
                    break;
                default:
                    break;
            }
        }

        if (render) {
            graphics_task(pax_buffer, ili9341, framebuffer, menu, NULL);
            render = false;
        }
        
        if (menuArgs != NULL) {
            *appfs_fd = menuArgs->fd;
            *menu_action = menuArgs->action;
            break;
        }
    }
    
    for (size_t index = 0; index < menu_get_length(menu); index++) {
        free(menu_get_callback_args(menu, index));
    }
    
    menu_free(menu);
}

void menu_wifi_settings(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, uint8_t* framebuffer, menu_action_t* menu_action) {
    menu_t* menu = menu_alloc("WiFi settings");
    *menu_action = ACTION_NONE;

    menu_args_t* wifi_scan_args = malloc(sizeof(menu_args_t));
    wifi_scan_args->action = ACTION_WIFI_SCAN;
    menu_insert_item(menu, "Add by scan...", NULL, wifi_scan_args, -1);
    
    menu_args_t* wifi_manual_args = malloc(sizeof(menu_args_t));
    wifi_manual_args->action = ACTION_WIFI_MANUAL;
    menu_insert_item(menu, "Add manually...", NULL, wifi_manual_args, -1);
    
    menu_args_t* wifi_list_args = malloc(sizeof(menu_args_t));
    wifi_list_args->action = ACTION_WIFI_LIST;
    menu_insert_item(menu, "List known networks", NULL, wifi_list_args, -1);
    
    menu_args_t* back_args = malloc(sizeof(menu_args_t));
    back_args->action = ACTION_BACK;
    menu_insert_item(menu, "< Back", NULL, back_args, -1);

    bool render = true;
    menu_args_t* menuArgs = NULL;

    while (1) {
        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(buttonQueue, &buttonMessage, 16 / portTICK_PERIOD_MS) == pdTRUE) {
            uint8_t pin = buttonMessage.input;
            bool value = buttonMessage.state;
            switch(pin) {
                case RP2040_INPUT_JOYSTICK_DOWN:
                    if (value) {
                        menu_navigate_next(menu);
                        render = true;
                    }
                    break;
                case RP2040_INPUT_JOYSTICK_UP:
                    if (value) {
                        menu_navigate_previous(menu);
                        render = true;
                    }
                    break;
                case RP2040_INPUT_BUTTON_ACCEPT:
                    if (value) {
                        menuArgs = menu_get_callback_args(menu, menu_get_position(menu));
                    }
                    break;
                default:
                    break;
            }
        }

        if (render) {
            graphics_task(pax_buffer, ili9341, framebuffer, menu, NULL);
            render = false;
        }
        
        if (menuArgs != NULL) {
            *menu_action = menuArgs->action;
            break;
        }
    }
    
    for (size_t index = 0; index < menu_get_length(menu); index++) {
        free(menu_get_callback_args(menu, index));
    }
    
    menu_free(menu);
}

void flush_stdin() {
    while (true) {
        char in = getc(stdin);
        if (in == 0xFF) {
            break;
        }
    }
}

bool read_stdin(uint8_t* buffer, uint32_t len, uint32_t timeout) {
    uint8_t index = 0;
    uint32_t timeout_counter = 0;
    while (index < len) {
        char in = getc(stdin);
        if (in != 0xFF) {
            buffer[index] = in;
            index++;
        } else {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            timeout_counter+=10;
            if (timeout_counter > timeout) {
                return false;
            }
        }
    }
    return true;
}

bool rp2040_bl_get_info(uint32_t* flash_start, uint32_t* flash_size, uint32_t* erase_size, uint32_t* write_size, uint32_t* max_data_len) {
    flush_stdin();
    printf("INFO");
    uint8_t rx_buffer[4 * 6];
    read_stdin(rx_buffer, sizeof(rx_buffer), 1000);
    if (memcmp(rx_buffer, "OKOK", 4) != 0) return false;
    memcpy((uint8_t*) flash_start,  &rx_buffer[4 * 1], 4);
    memcpy((uint8_t*) flash_size,   &rx_buffer[4 * 2], 4);
    memcpy((uint8_t*) erase_size,   &rx_buffer[4 * 3], 4);
    memcpy((uint8_t*) write_size,   &rx_buffer[4 * 4], 4);
    memcpy((uint8_t*) max_data_len, &rx_buffer[4 * 5], 4);
    return true;
}

bool rp2040_bl_erase(uint32_t address, uint32_t length) {
    printf("ERAS");
    uint8_t* addres_u8 = &address;
    putc(addres_u8[0], stdout);
    putc(addres_u8[1], stdout);
    putc(addres_u8[2], stdout);
    putc(addres_u8[3], stdout);
    uint8_t* length_u8 = &length;
    putc(length_u8[0], stdout);
    putc(length_u8[1], stdout);
    putc(length_u8[2], stdout);
    putc(length_u8[3], stdout);
    flush_stdin();
    uint8_t rx_buffer[4];
    read_stdin(rx_buffer, sizeof(rx_buffer), 10000);
    if (memcmp(rx_buffer, "OKOK", 4) != 0) return false;
    return true;
}

bool rp2040_bl_write(uint32_t address, uint32_t length, uint8_t* data, uint32_t* crc) {
    flush_stdin();
    printf("WRIT");
    uint8_t* addres_u8 = &address;
    putc(addres_u8[0], stdout);
    putc(addres_u8[1], stdout);
    putc(addres_u8[2], stdout);
    putc(addres_u8[3], stdout);
    uint8_t* length_u8 = &length;
    putc(length_u8[0], stdout);
    putc(length_u8[1], stdout);
    putc(length_u8[2], stdout);
    putc(length_u8[3], stdout);
    
    for (uint32_t index = 0; index < length; index++) {
        putc(data[index], stdout);
    }
    
    uint8_t rx_buffer[8];
    uint16_t timeout = 0;
    
    uint8_t index = 0;
    while (index < sizeof(rx_buffer)) {
        char in = getc(stdin);
        if (in != 0xFF) {
            rx_buffer[index] = in;
            index++;
        } else {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            timeout++;
            if (timeout > 500) {
                return false;
            }
        }
    }
    
    if (memcmp(rx_buffer, "OKOK", 4) != 0) {
        return false;
    }
    
    memcpy((uint8_t*) crc, &rx_buffer[4 * 1], 4);
    return true;
}

void app_main(void) {
    flush_stdin();
    esp_err_t res;
    
    /* Initialize memory */
    uint8_t* framebuffer = heap_caps_malloc(ILI9341_BUFFER_SIZE, MALLOC_CAP_8BIT);
    if (framebuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer");
        restart();
    }
    memset(framebuffer, 0, ILI9341_BUFFER_SIZE);
    
    pax_buf_t* pax_buffer = malloc(sizeof(pax_buf_t));
    if (framebuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate pax buffer");
        restart();
    }
    memset(pax_buffer, 0, sizeof(pax_buf_t));
    
    pax_buf_init(pax_buffer, framebuffer, ILI9341_WIDTH, ILI9341_HEIGHT, PAX_BUF_16_565RGB);
    driver_framebuffer_init(framebuffer);
    
    /* Initialize hardware */
    
    bool lcdReady = false;
    res = board_init(&lcdReady);
    
    if (res != ESP_OK) {
        if (lcdReady) {
            ILI9341* ili9341 = get_ili9341();
            graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Hardware error!");
        }
        printf("Failed to initialize hardware!\n");
        restart();
    }
    
    ILI9341* ili9341 = get_ili9341();
    ICE40* ice40 = get_ice40();
    BNO055* bno055 = get_bno055();
    RP2040* rp2040 = get_rp2040();

    graphics_task(pax_buffer, ili9341, framebuffer, NULL, "AppFS init...");
    res = appfs_init();
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "AppFS init failed: %d", res);
        graphics_task(pax_buffer, ili9341, framebuffer, NULL, "AppFS init failed!");
        return;
    }
    ESP_LOGI(TAG, "AppFS initialized");
    
    graphics_task(pax_buffer, ili9341, framebuffer, NULL, "NVS init...");
    res = nvs_init();
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %d", res);
        graphics_task(pax_buffer, ili9341, framebuffer, NULL, "NVS init failed!");
        return;
    }
    ESP_LOGI(TAG, "NVS initialized");
    
    graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Mount SD card...");
    res = mount_sd(SD_CMD, SD_CLK, SD_D0, SD_PWR, "/sd", false, 5);
    bool sdcard_ready = (res == ESP_OK);
  
    if (sdcard_ready) {
        graphics_task(pax_buffer, ili9341, framebuffer, NULL, "SD card mounted");
    }
    
    ws2812_init(GPIO_LED_DATA);
    uint8_t ledBuffer[15] = {50, 0, 0, 50, 0, 0, 50, 0, 0, 50, 0, 0, 50, 0, 0};
    ws2812_send_data(ledBuffer, sizeof(ledBuffer));
    
    uint8_t fw_version;
    if (rp2040_get_firmware_version(rp2040, &fw_version) != ESP_OK) {
        graphics_task(pax_buffer, ili9341, framebuffer, NULL, "RP2040 FW VERSION READ FAILED");
        restart();
    }
    
    if (fw_version == 0xFF) {
        // RP2040 is in bootloader mode
        char buffer[255] = {0};
        graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Updating RP2040...");
        vTaskDelay(1 / portTICK_PERIOD_MS);
        uint8_t bl_version;
        if (rp2040_get_bootloader_version(rp2040, &bl_version) != ESP_OK) {
            graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Communication error (1)");
            restart();
        }
        if (bl_version != 0x01) {
            graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Unknown BL version");
            restart();
        }
        graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Waiting for bootloader...");
        while (true) {
            vTaskDelay(1 / portTICK_PERIOD_MS);
            uint8_t bl_state;
            if (rp2040_get_bootloader_state(rp2040, &bl_state) != ESP_OK) {
                graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Communication error (2)");
                restart();
            }
            if (bl_state == 0xB0) {
                graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Bootloader ready");
                break;
            }
            if (bl_state > 0xB0) {
                graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Unknown BL state");
                restart();
            }
        }
        
        graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Sync to BL...");
        char rx_buffer[16];
        uint8_t rx_buffer_pos = 0;
        memset(rx_buffer, 0, sizeof(rx_buffer));
        while (true) {
            printf("SYNC");
            
            char in = 0xFF;
            do {
                in = getc(stdin);
                if (in != 0xFF) {
                    rx_buffer[rx_buffer_pos] = in;
                    rx_buffer_pos++;
                }
            } while ((in != 0xFF) && (rx_buffer_pos < sizeof(rx_buffer)));
            
            if (memcmp(rx_buffer, "PICO", 4) == 0) {
                break;
            }
            
            if (rx_buffer_pos >= 4) {
                for (uint8_t i = 0; i < sizeof(rx_buffer) - 1; i++) {
                    rx_buffer[i] = rx_buffer[i+1];
                    if (memcmp(rx_buffer, "PICO", 4) == 0) {
                        break;
                    }
                }
                rx_buffer_pos--;
            }
            
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }
        graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Synced to BL!");
        
        char in = 0xFF;
        do {
            in = getc(stdin);
        } while (in != 0xFF);
        
        uint32_t flash_start = 0, flash_size = 0, erase_size = 0, write_size = 0, max_data_len = 0;
        
        bool success = rp2040_bl_get_info(&flash_start, &flash_size, &erase_size, &write_size, &max_data_len);
        
        if (!success) {
            graphics_task(pax_buffer, ili9341, framebuffer, NULL, "BL INFO FAIL");
            restart();
        }
        
        pax_noclip(pax_buffer);
        pax_background(pax_buffer, 0xCCCCCC);
        char message[64];
        memset(message, 0, sizeof(message));
        snprintf(message, sizeof(message) - 1, "Flash start: 0x%08X", flash_start);
        pax_draw_text(pax_buffer, 0xFF000000, NULL, 18, 0, 20*0, message);
        snprintf(message, sizeof(message) - 1, "Flash size : 0x%08X", flash_size);
        pax_draw_text(pax_buffer, 0xFF000000, NULL, 18, 0, 20*1, message);
        snprintf(message, sizeof(message) - 1, "Erase size : 0x%08X", erase_size);
        pax_draw_text(pax_buffer, 0xFF000000, NULL, 18, 0, 20*2, message);
        snprintf(message, sizeof(message) - 1, "Write size : 0x%08X", write_size);
        pax_draw_text(pax_buffer, 0xFF000000, NULL, 18, 0, 20*3, message);
        snprintf(message, sizeof(message) - 1, "Max data ln: 0x%08X", max_data_len);
        pax_draw_text(pax_buffer, 0xFF000000, NULL, 18, 0, 20*4, message);
        ili9341_write(ili9341, framebuffer);
        
        bool eraseSuccess = rp2040_bl_erase(flash_start, flash_size - erase_size);
        snprintf(message, sizeof(message) - 1, "Erase      : %s", eraseSuccess ? "YES" : "NO");
        pax_draw_text(pax_buffer, eraseSuccess ? 0xFF000000 : 0xFFFF0000, NULL, 18, 0, 20*5, message);
        ili9341_write(ili9341, framebuffer);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        
        if (!eraseSuccess) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            restart();
        }
        
        for (uint32_t index = 0; index < sizeof(mch2022_firmware_bin);) {
            uint32_t crc = 0;
            uint32_t txLength = write_size;
            if (txLength > (sizeof(mch2022_firmware_bin) - index)) {
                txLength = sizeof(mch2022_firmware_bin) - index;
            }
            pax_noclip(pax_buffer);
            pax_background(pax_buffer, 0xCCCCCC);
            char message[64];
            memset(message, 0, sizeof(message));
            snprintf(message, sizeof(message) - 1, "Write to : 0x%08X", 0x10010000 + index);
            pax_draw_text(pax_buffer, 0xFF000000, NULL, 18, 0, 20*0, message);
            snprintf(message, sizeof(message) - 1, "Write len: 0x%08X", txLength);
            pax_draw_text(pax_buffer, 0xFF000000, NULL, 18, 0, 20*1, message);
            ili9341_write(ili9341, framebuffer);
            bool writeSuccess = rp2040_bl_write(0x10010000 + index, txLength, &mch2022_firmware_bin[index], &crc);
            if (!writeSuccess) {
                pax_noclip(pax_buffer);
                pax_background(pax_buffer, 0xCCCCCC);
                char message[64];
                memset(message, 0, sizeof(message));
                snprintf(message, sizeof(message) - 1, "Write err: 0x%08X", 0x10010000 + index);
                pax_draw_text(pax_buffer, 0xFFFF0000, NULL, 18, 0, 20*0, message);
                snprintf(message, sizeof(message) - 1, "Write len: 0x%08X", txLength);
                pax_draw_text(pax_buffer, 0xFF000000, NULL, 18, 0, 20*1, message);
                ili9341_write(ili9341, framebuffer);
                restart();
            }
            index += write_size;
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }        

        while (true) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        
        /*while (true) {
            uint8_t bl_state;
            if (rp2040_get_bootloader_state(rp2040, &bl_state) != ESP_OK) {
                graphics_task(pax_buffer, ili9341, framebuffer, NULL, "RP2040 BL STATE READ FAILED");
                restart();
            }
            snprintf(buffer, sizeof(buffer), "RP2040 BL (%02X): %02X", bl_version, bl_state);
            graphics_task(pax_buffer, ili9341, framebuffer, NULL, buffer);
            vTaskDelay(200 / portTICK_PERIOD_MS);
            if (bl_state == 0xB0) {
                printf("SYNC");
            }
        }
        return;*/
    }
    
    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0xCCCCCC);
    char message[64];
    memset(message, 0, sizeof(message));
    snprintf(message, sizeof(message) - 1, "RP2040 firmware: 0x%02X", fw_version);
    pax_draw_text(pax_buffer, 0xFF000000, NULL, 18, 0, 20*0, message);
    ili9341_write(ili9341, framebuffer);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    /*while (true) {
        fpga_test(ili9341, ice40, rp2040->queue);
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }*/
    
    /*while (true) {
        uint16_t state;
        rp2040_read_buttons(rp2040, &state);
        printf("Button state: %04X\n", state);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        ledBuffer[1] = 255;
        ws2812_send_data(ledBuffer, sizeof(ledBuffer));
        vTaskDelay(200 / portTICK_PERIOD_MS);
        ledBuffer[1] = 0;
        ledBuffer[0] = 255;
        ws2812_send_data(ledBuffer, sizeof(ledBuffer));
        fpga_test(ili9341, ice40, rp2040->queue);
        ledBuffer[0] = 0;
        ws2812_send_data(ledBuffer, sizeof(ledBuffer));
    }*/

    while (true) {
        menu_action_t menu_action;
        appfs_handle_t appfs_fd;
        menu_launcher(rp2040->queue, pax_buffer, ili9341, framebuffer, &menu_action, &appfs_fd);
        if (menu_action == ACTION_APPFS) {
            appfs_boot_app(appfs_fd);
        } else if (menu_action == ACTION_FPGA) {
            graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Loading...");
            fpga_test(ili9341, ice40, rp2040->queue);
        } else if (menu_action == ACTION_RP2040_BL) {
            graphics_task(pax_buffer, ili9341, framebuffer, NULL, "RP2040 update...");
            rp2040_reboot_to_bootloader(rp2040);
            esp_restart();
        } else if (menu_action == ACTION_INSTALLER) {
            graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Installing...");
            appfs_store_app(pax_buffer, ili9341, framebuffer);
         } else if (menu_action == ACTION_WIFI_CONNECT) {
            graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Connecting...");
            nvs_handle_t handle;
            nvs_open("system", NVS_READWRITE, &handle);
            char ssid[33];
            char password[33];
            size_t requiredSize;
            esp_err_t res = nvs_get_str(handle, "wifi.ssid", NULL, &requiredSize);
            if (res != ESP_OK) {
                strcpy(ssid, "");
            } else if (requiredSize < sizeof(ssid)) {
                res = nvs_get_str(handle, "wifi.ssid", ssid, &requiredSize);
                if (res != ESP_OK) strcpy(ssid, "");
                res = nvs_get_str(handle, "wifi.password", NULL, &requiredSize);
                if (res != ESP_OK) {
                    strcpy(password, "");
                } else if (requiredSize < sizeof(password)) {
                    res = nvs_get_str(handle, "wifi.password", password, &requiredSize);
                    if (res != ESP_OK) strcpy(password, "");
                }
            }
            nvs_close(&handle);
            wifi_init(ssid, password, WIFI_AUTH_WPA2_PSK, 3);
        } else if (menu_action == ACTION_OTA) {
            graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Firmware update...");
        } else if (menu_action == ACTION_SETTINGS) {
            while (true) {
                menu_wifi_settings(rp2040->queue, pax_buffer, ili9341, framebuffer, &menu_action);
                if (menu_action == ACTION_WIFI_MANUAL) {
                    nvs_handle_t handle;
                    nvs_open("system", NVS_READWRITE, &handle);
                    char ssid[33];
                    char password[33];
                    size_t requiredSize;
                    esp_err_t res = nvs_get_str(handle, "wifi.ssid", NULL, &requiredSize);
                    if (res != ESP_OK) {
                        strcpy(ssid, "");
                        strcpy(password, "");
                    } else if (requiredSize < sizeof(ssid)) {
                        res = nvs_get_str(handle, "wifi.ssid", ssid, &requiredSize);
                        if (res != ESP_OK) strcpy(ssid, "");
                        res = nvs_get_str(handle, "wifi.password", NULL, &requiredSize);
                        if (res != ESP_OK) {
                            strcpy(password, "");
                        } else if (requiredSize < sizeof(password)) {
                            res = nvs_get_str(handle, "wifi.password", password, &requiredSize);
                            if (res != ESP_OK) strcpy(password, "");
                        }
                    }
                    bool accepted = keyboard(rp2040->queue, pax_buffer, ili9341, framebuffer, 30, 30, pax_buffer->width - 60, pax_buffer->height - 60, "WiFi SSID", "Press HOME to exit", ssid, sizeof(ssid));
                    if (accepted) {
                        accepted = keyboard(rp2040->queue, pax_buffer, ili9341, framebuffer, 30, 30, pax_buffer->width - 60, pax_buffer->height - 60, "WiFi password", "Press HOME to exit", password, sizeof(password));
                    }
                    if (accepted) {
                        nvs_set_str(handle, "wifi.ssid", ssid);
                        nvs_set_str(handle, "wifi.password", password);
                        graphics_task(pax_buffer, ili9341, framebuffer, NULL, "WiFi settings stored");
                    } else {
                        graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Canceled");
                    }
                    nvs_close(&handle);
                } else if (menu_action == ACTION_WIFI_LIST) {
                    nvs_handle_t handle;
                    nvs_open("system", NVS_READWRITE, &handle);
                    char ssid[33];
                    char password[33];
                    size_t requiredSize;
                    esp_err_t res = nvs_get_str(handle, "wifi.ssid", NULL, &requiredSize);
                    if (res != ESP_OK) {
                        strcpy(ssid, "");
                    } else if (requiredSize < sizeof(ssid)) {
                        res = nvs_get_str(handle, "wifi.ssid", ssid, &requiredSize);
                        if (res != ESP_OK) strcpy(ssid, "");
                        res = nvs_get_str(handle, "wifi.password", NULL, &requiredSize);
                        if (res != ESP_OK) {
                            strcpy(password, "");
                        } else if (requiredSize < sizeof(password)) {
                            res = nvs_get_str(handle, "wifi.password", password, &requiredSize);
                            if (res != ESP_OK) strcpy(password, "");
                        }
                    }
                    nvs_close(&handle);
                    char buffer[300];
                    snprintf(buffer, sizeof(buffer), "SSID is %s\nPassword is %s", ssid, password);
                    graphics_task(pax_buffer, ili9341, framebuffer, NULL, buffer);
                } else {
                    break;
                }
            }
        }
    }

    
    free(framebuffer);
}
