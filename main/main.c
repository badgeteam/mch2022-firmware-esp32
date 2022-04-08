#include <stdio.h>
#include <string.h>
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_system.h>
//#include <esp_spi_flash.h>
#include <esp_err.h>
#include <esp_log.h>
#include "hardware.h"
#include "managed_i2c.h"
#include "pax_gfx.h"
#include "sdcard.h"
#include "appfs.h"
#include "driver_framebuffer.h"

#include "esp_sleep.h"
#include "soc/rtc.h"
#include "soc/rtc_cntl_reg.h"

#include "rp2040.h"

#include "fpga_test.h"

#include "menu.h"
#include "button_message.h"

static const char *TAG = "main";

bool calibrate = true;
bool display_bno_value = false;
ILI9341* ili9341 = NULL;
ICE40* ice40 = NULL;
BNO055* bno055 = NULL;
RP2040* rp2040 = NULL;
uint8_t* framebuffer = NULL;
pax_buf_t* pax_buffer = NULL;
xQueueHandle buttonQueue;

typedef enum action {
    ACTION_NONE,
    ACTION_APPFS,
    ACTION_INSTALLER,
    ACTION_FPGA
} menu_action_t;

typedef struct _menu_args {
    appfs_handle_t fd;
    menu_action_t action;
} menu_args_t;

void button_handler(uint8_t pin, bool value) {
    button_message_t message;
    message.button = pin;
    message.state = value;
    xQueueSend(buttonQueue, &message, portMAX_DELAY);
}

void button_init() {
    PCA9555* pca9555 = get_pca9555();   
    pca9555_set_interrupt_handler(pca9555, PCA9555_PIN_BTN_START, button_handler);
    pca9555_set_interrupt_handler(pca9555, PCA9555_PIN_BTN_SELECT, button_handler);
    pca9555_set_interrupt_handler(pca9555, PCA9555_PIN_BTN_MENU, button_handler);
    pca9555_set_interrupt_handler(pca9555, PCA9555_PIN_BTN_HOME, button_handler);
    pca9555_set_interrupt_handler(pca9555, PCA9555_PIN_BTN_JOY_LEFT, button_handler);
    pca9555_set_interrupt_handler(pca9555, PCA9555_PIN_BTN_JOY_PRESS, button_handler);
    pca9555_set_interrupt_handler(pca9555, PCA9555_PIN_BTN_JOY_DOWN, button_handler);
    pca9555_set_interrupt_handler(pca9555, PCA9555_PIN_BTN_JOY_UP, button_handler);
    pca9555_set_interrupt_handler(pca9555, PCA9555_PIN_BTN_JOY_RIGHT, button_handler);
    pca9555_set_interrupt_handler(pca9555, PCA9555_PIN_BTN_BACK, button_handler);
    pca9555_set_interrupt_handler(pca9555, PCA9555_PIN_BTN_ACCEPT, button_handler);
}

void restart() {
    for (int i = 3; i >= 0; i--) {
        printf("Restarting in %d seconds...\n", i);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();
}

void message_render(pax_buf_t *aBuffer, char* message, float aPosX, float aPosY, float aWidth, float aHeight) {
    pax_col_t fgColor = 0xFFFF0000;
    pax_col_t bgColor = 0xFFFFD4D4;
    pax_clip(aBuffer, aPosX, aPosY, aWidth, aHeight);
    pax_simple_rect(aBuffer, bgColor, aPosX, aPosY, aWidth, aHeight);
    pax_outline_rect(aBuffer, fgColor, aPosX, aPosY, aWidth, aHeight);
    pax_clip(aBuffer, aPosX + 1, aPosY + 1, aWidth - 2, aHeight - 2);
    pax_draw_text(aBuffer, fgColor, NULL, 18, aPosX + 1, aPosY + 1, message);
    pax_noclip(aBuffer);
}


esp_err_t graphics_task(pax_buf_t* buffer, ILI9341* ili9341, uint8_t* framebuffer, menu_t* menu, char* message) {
    pax_background(pax_buffer, 0xCCCCCC);
    if (menu != NULL) {
        menu_render(pax_buffer, menu, 10, 10, 320-20, 240-20);
    }
    
    if (message != NULL) {
        message_render(pax_buffer, message, 20, 110, 320-40, 20);
    }

    return ili9341_write(ili9341, framebuffer);
}

esp_err_t draw_message(char* message) {
    pax_background(pax_buffer, 0xFFFFFF);
    pax_draw_text(pax_buffer, pax_col_rgb(0,0,0), PAX_FONT_DEFAULT, 18, 0, 0, message);
    return ili9341_write(ili9341, framebuffer);
}

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

void appfs_store_app(void) {
    draw_message("Installing app...");
    esp_err_t res;
    appfs_handle_t handle;
    FILE* app_fd = fopen("/sd/gnuboy.bin", "rb");
    if (app_fd == NULL) {
        draw_message("Failed to open gnuboy.bin");
        ESP_LOGE(TAG, "Failed to open gnuboy.bin");
        vTaskDelay(100 / portTICK_PERIOD_MS);
        return;
    }
    size_t app_size;
    uint8_t* app = load_file_to_ram(app_fd, &app_size);
    if (app == NULL) {
        draw_message("Failed to load app to RAM");
        ESP_LOGE(TAG, "Failed to load application into RAM");
        vTaskDelay(100 / portTICK_PERIOD_MS);
        return;
    }
    
    ESP_LOGI(TAG, "Application size %d", app_size);
    
    res = appfsCreateFile("gnuboy", app_size, &handle);
    if (res != ESP_OK) {
        draw_message("Failed to create on AppFS");
        ESP_LOGE(TAG, "Failed to create file on AppFS (%d)", res);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        free(app);
        return;
    }
    res = appfsWrite(handle, 0, app, app_size);
    if (res != ESP_OK) {
        draw_message("Failed to write to AppFS");
        ESP_LOGE(TAG, "Failed to write to file on AppFS (%d)", res);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        free(app);
        return;
    }
    free(app);
    ESP_LOGI(TAG, "Application is now stored in AppFS");
    draw_message("App installed!");
    vTaskDelay(100 / portTICK_PERIOD_MS);
    return;
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

void appfs_test(bool sdcard_ready) {
    appfs_handle_t fd = appfsOpen("gnuboy");
    if (fd < 0) {
        ESP_LOGW(TAG, "gnuboy not found in appfs");
        draw_message("gnuboy not found in fs!");
        /*if (sdcard_ready) {
            appfs_store_app();
            appfs_test(false); // Recursive, but who cares :D
        }*/
    } else {
        draw_message("Booting gnuboy...");
        ESP_LOGE(TAG, "booting gnuboy from appfs (%d)", fd);
        appfs_boot_app(fd);
    }
}

void app_main(void) {
    esp_err_t res;
    
    buttonQueue = xQueueCreate(10, sizeof(button_message_t));
    
    if (buttonQueue == NULL) {
        ESP_LOGE(TAG, "Failed to allocate queue");
        restart();
    }
    
    
    framebuffer = heap_caps_malloc(ILI9341_BUFFER_SIZE, MALLOC_CAP_8BIT);
    if (framebuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer");
        restart();
    }
    memset(framebuffer, 0, ILI9341_BUFFER_SIZE);
    
    pax_buffer = malloc(sizeof(pax_buf_t));
    if (framebuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate pax buffer");
        restart();
    }
    memset(pax_buffer, 0, sizeof(pax_buf_t));
    
    pax_buf_init(pax_buffer, framebuffer, ILI9341_WIDTH, ILI9341_HEIGHT, PAX_BUF_16_565RGB);
    driver_framebuffer_init(framebuffer);
    
    res = board_init();
    
    if (res != ESP_OK) {
        printf("Failed to initialize hardware!\n");
        restart();
    }
    
    ili9341 = get_ili9341();
    ice40 = get_ice40();
    bno055 = get_bno055();
    rp2040 = get_rp2040();

    graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Button init...");
    button_init();
    
    rp2040_set_led_mode(rp2040, true, true);
    
    graphics_task(pax_buffer, ili9341, framebuffer, NULL, "AppFS init...");
    res = appfs_init();
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "AppFS init failed: %d", res);
        return;
    }
    ESP_LOGI(TAG, "AppFS initialized");
    
    graphics_task(pax_buffer, ili9341, framebuffer, NULL, "Mount SD card...");
    res = mount_sd(SD_CMD, SD_CLK, SD_D0, SD_PWR, "/sd", false, 5);
    bool sdcard_ready = (res == ESP_OK);
  
    if (sdcard_ready) {
        graphics_task(pax_buffer, ili9341, framebuffer, NULL, "SD card mounted");
    } else {
        graphics_task(pax_buffer, ili9341, framebuffer, NULL, "No SD card");
    }

    menu_t* menu = menu_alloc("Launcher");    
    
    appfs_handle_t current_fd = APPFS_INVALID_FD;
    
    while (1) {
        current_fd = appfsNextEntry(current_fd);
        if (current_fd == APPFS_INVALID_FD) break;
        
        const char* name = NULL;
        appfsEntryInfo(current_fd, &name, NULL);
        menu_args_t* args = malloc(sizeof(menu_args_t));
        args->fd = current_fd;
        args->action = ACTION_APPFS;
        menu_insert_item(menu, name, NULL, (void*) args, -1);
    }
    
    menu_args_t* fpga_args = malloc(sizeof(menu_args_t));
    fpga_args->action = ACTION_FPGA;
    menu_insert_item(menu, "FPGA test", NULL, fpga_args, -1);
    menu_args_t* install_args = malloc(sizeof(menu_args_t));
    install_args->action = ACTION_INSTALLER;
    menu_insert_item(menu, "Install app...", NULL, install_args, -1);

    bool render = true;
    menu_args_t* menuAction = NULL;
    while (1) {
        button_message_t buttonMessage = {0};
        if (xQueueReceive(buttonQueue, &buttonMessage, 16 / portTICK_PERIOD_MS) == pdTRUE) {
            uint8_t pin = buttonMessage.button;
            bool value = buttonMessage.state;
            switch(pin) {
                case PCA9555_PIN_BTN_JOY_LEFT:
                    printf("Joystick horizontal %s\n", value ? "left" : "center");
                    break;
                case PCA9555_PIN_BTN_JOY_PRESS:
                    printf("Joystick %s\n", value ? "pressed" : "released");
                    break;
                case PCA9555_PIN_BTN_JOY_DOWN:
                    printf("Joystick vertical %s\n", value ? "down" : "center");
                    if (value) {
                        menu_navigate_next(menu);
                        render = true;
                    }
                    break;
                case PCA9555_PIN_BTN_JOY_UP:
                    printf("Joystick vertical %s\n", value ? "up" : "center");
                    if (value) {
                        menu_navigate_previous(menu);
                        render = true;
                    }
                    break;
                case PCA9555_PIN_BTN_JOY_RIGHT:
                    printf("Joystick horizontal %s\n", value ? "right" : "center");
                    break;
                case PCA9555_PIN_BTN_HOME:
                    printf("Home button %s\n", value ? "pressed" : "released");
                    break;
                case PCA9555_PIN_BTN_MENU:
                    printf("Menu button %s\n", value ? "pressed" : "released");
                    //if (value) reset_to_menu = true;
                    break;
                case PCA9555_PIN_BTN_START: {
                    printf("Start button %s\n", value ? "pressed" : "released");
                    break;
                }
                case PCA9555_PIN_BTN_SELECT: {
                    printf("Select button %s\n", value ? "pressed" : "released");
                    break;
                }
                case PCA9555_PIN_BTN_BACK:
                    printf("Back button %s\n", value ? "pressed" : "released");
                    break;
                case PCA9555_PIN_BTN_ACCEPT:
                    printf("Accept button %s\n", value ? "pressed" : "released");
                    if (value) {
                        menuAction = menu_get_callback_args(menu, menu_get_position(menu));
                        printf("Position: %u\n", menu_get_position(menu));
                    }
                    break;
                default:
                    printf("Unknown button %d %s\n", pin, value ? "pressed" : "released");
            }
        }

        if (render) {
            graphics_task(pax_buffer, ili9341, framebuffer, menu, NULL);
            render = false;
        }
        
        if (menuAction != NULL) {
            graphics_task(pax_buffer, ili9341, framebuffer, menu, "Please wait...");
            if (menuAction->action == ACTION_APPFS) {
                appfs_boot_app(menuAction->fd);
            } else if (menuAction->action == ACTION_FPGA) {
                graphics_task(pax_buffer, ili9341, framebuffer, menu, "FPGA TEST");
                fpga_test(ili9341, ice40, buttonQueue);
            }else if (menuAction->action == ACTION_INSTALLER) {
                graphics_task(pax_buffer, ili9341, framebuffer, menu, "INSTALLER");
                appfs_store_app();
            }
            menuAction = NULL;
            render = true;
        }
    }

    
    free(framebuffer);
}
