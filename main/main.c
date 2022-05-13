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

#include "rp2040.h"
#include "rp2040bl.h"

#include "fpga_test.h"

#include "menu.h"
#include "system_wrapper.h"
#include "graphics_wrapper.h"
#include "appfs_wrapper.h"
#include "settings.h"
#include "wifi_connection.h"
#include "rp2040_updater.h"

#include "ws2812.h"

#include "esp32/rom/crc.h"

#include "efuse.h"

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

void display_boot_screen(pax_buf_t* pax_buffer, ILI9341* ili9341) {
    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0x325aa8);
    pax_draw_text(pax_buffer, 0xFFFFFFFF, NULL, 18, 0, 20*0, "Starting launcher...");
    ili9341_write(ili9341, pax_buffer->buf);
}

void display_fatal_error(pax_buf_t* pax_buffer, ILI9341* ili9341, const char* line0, const char* line1, const char* line2, const char* line3) {
    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0xa85a32);
    if (line0 != NULL) pax_draw_text(pax_buffer, 0xFFFFFFFF, NULL, 18, 0, 20*0, line0);
    if (line1 != NULL) pax_draw_text(pax_buffer, 0xFFFFFFFF, NULL, 12, 0, 20*1, line1);
    if (line2 != NULL) pax_draw_text(pax_buffer, 0xFFFFFFFF, NULL, 12, 0, 20*2, line2);
    if (line3 != NULL) pax_draw_text(pax_buffer, 0xFFFFFFFF, NULL, 12, 0, 20*3, line3);
    ili9341_write(ili9341, pax_buffer->buf);
}

void app_main(void) {
    esp_err_t res;
    
    /* Initialize memory */
    uint8_t* framebuffer = heap_caps_malloc(ILI9341_BUFFER_SIZE, MALLOC_CAP_8BIT);
    if (framebuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer");
        esp_restart();
    }
    memset(framebuffer, 0, ILI9341_BUFFER_SIZE);
    
    pax_buf_t* pax_buffer = malloc(sizeof(pax_buf_t));
    if (framebuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate buffer for PAX graphics library");
        esp_restart();
    }
    memset(pax_buffer, 0, sizeof(pax_buf_t));
    
    pax_buf_init(pax_buffer, framebuffer, ILI9341_WIDTH, ILI9341_HEIGHT, PAX_BUF_16_565RGB);
    
    /* Initialize hardware */
    
    efuse_protect();
    
    if (bsp_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize basic board support functions");
        esp_restart();
    }
    
    ILI9341* ili9341 = get_ili9341();
    if (ili9341 == NULL) {
        ESP_LOGE(TAG, "ili9341 is NULL");
        esp_restart();
    }

    
    display_boot_screen(pax_buffer, ili9341);
    
    if (bsp_rp2040_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize the RP2040 co-processor");
        display_fatal_error(pax_buffer, ili9341, "Failed to initialize", "RP2040 co-processor error", NULL, NULL);
        esp_restart();
    }
    
    RP2040* rp2040 = get_rp2040();
    if (rp2040 == NULL) {
        ESP_LOGE(TAG, "rp2040 is NULL");
        esp_restart();
    }

    rp2040_updater(rp2040, pax_buffer, ili9341, framebuffer); // Handle RP2040 firmware update & bootloader mode
    
    if (bsp_ice40_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize the ICE40 FPGA");
        display_fatal_error(pax_buffer, ili9341, "Failed to initialize", "ICE40 FPGA error", NULL, NULL);
        esp_restart();
    }
    
    ICE40* ice40 = get_ice40();
    if (ice40 == NULL) {
        ESP_LOGE(TAG, "ice40 is NULL");
        esp_restart();
    }
    
    if (bsp_bno055_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize the BNO055 position sensor");
        display_fatal_error(pax_buffer, ili9341, "Failed to initialize", "BNO055 sensor error", "Check I2C bus", "Remove SAO and try again");
        esp_restart();
    }

    BNO055* bno055 = get_bno055();
    if (bno055 == NULL) {
        ESP_LOGE(TAG, "bno055 is NULL");
        esp_restart();
    }

    /* Start AppFS */
    res = appfs_init();
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "AppFS init failed: %d", res);
        display_fatal_error(pax_buffer, ili9341, "Failed to initialize", "AppFS failed to initialize", "Flash may be corrupted", NULL);
        esp_restart();
    }
    
    /* Start NVS */
    res = nvs_init();
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %d", res);
        display_fatal_error(pax_buffer, ili9341, "Failed to initialize", "NVS failed to initialize", "Flash may be corrupted", NULL);
        esp_restart();
    }
    
    /* Start SD card */
    res = mount_sd(SD_CMD, SD_CLK, SD_D0, SD_PWR, "/sd", false, 5);
    bool sdcard_ready = (res == ESP_OK);

    /* Start LEDs */
    ws2812_init(GPIO_LED_DATA);
    uint8_t ledBuffer[15] = {50, 0, 0, 50, 0, 0, 50, 0, 0, 50, 0, 0, 50, 0, 0};
    ws2812_send_data(ledBuffer, sizeof(ledBuffer));

    /* Launcher menu */
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
