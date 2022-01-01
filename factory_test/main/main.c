#include <stdio.h>
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_spi_flash.h>
#include <esp_err.h>
#include <esp_log.h>
#include "hardware.h"
#include "pca9555.h"

static const char *TAG = "main";

bool calibrate = true;

void button_handler(uint8_t pin, bool value) {
    switch(pin) {
        case PCA9555_PIN_BTN_START:
            printf("Start button %s\n", value ? "pressed" : "released");
            break;
        case PCA9555_PIN_BTN_SELECT:
            printf("Select button %s\n", value ? "pressed" : "released");
            break;
        case PCA9555_PIN_BTN_MENU:
            printf("Menu button %s\n", value ? "pressed" : "released");
            break;
        case PCA9555_PIN_BTN_HOME:
            printf("Home button %s\n", value ? "pressed" : "released");
            break;
        case PCA9555_PIN_BTN_JOY_LEFT:
            printf("Joystick horizontal %s\n", value ? "left" : "center");
            break;
        case PCA9555_PIN_BTN_JOY_PRESS:
            printf("Joystick %s\n", value ? "pressed" : "released");
            break;
        case PCA9555_PIN_BTN_JOY_DOWN:
            printf("Joystick vertical %s\n", value ? "down" : "center");
            break;
        case PCA9555_PIN_BTN_JOY_UP:
            printf("Joy vertical %s\n", value ? "up" : "center");
            break;
        case PCA9555_PIN_BTN_JOY_RIGHT:
            printf("Joy horizontal %s\n", value ? "right" : "center");
            break;
        case PCA9555_PIN_BTN_BACK:
            printf("Back button %s\n", value ? "pressed" : "released");
            break;
        case PCA9555_PIN_BTN_ACCEPT:
            //printf("Accept button %s\n", value ? "pressed" : "released");
            if (value) calibrate = true;
            break;
        default:
            printf("Unknown button %d %s\n", pin, value ? "pressed" : "released");
    }
}

void button_init() {
    PCA9555* pca9555 = get_pca9555();
    pca9555_set_gpio_polarity(pca9555, PCA9555_PIN_BTN_START, true);
    pca9555_set_gpio_polarity(pca9555, PCA9555_PIN_BTN_SELECT, true);
    pca9555_set_gpio_polarity(pca9555, PCA9555_PIN_BTN_MENU, true);
    pca9555_set_gpio_polarity(pca9555, PCA9555_PIN_BTN_HOME, true);
    pca9555_set_gpio_polarity(pca9555, PCA9555_PIN_BTN_JOY_LEFT, true);
    pca9555_set_gpio_polarity(pca9555, PCA9555_PIN_BTN_JOY_PRESS, true);
    pca9555_set_gpio_polarity(pca9555, PCA9555_PIN_BTN_JOY_DOWN, true);
    pca9555_set_gpio_polarity(pca9555, PCA9555_PIN_BTN_JOY_UP, true);
    pca9555_set_gpio_polarity(pca9555, PCA9555_PIN_BTN_JOY_RIGHT, true);
    pca9555_set_gpio_polarity(pca9555, PCA9555_PIN_BTN_BACK, true);
    pca9555_set_gpio_polarity(pca9555, PCA9555_PIN_BTN_ACCEPT, true);
    
    pca9555->pin_state = 0; // Reset all pin states so that the interrupt function doesn't trigger all the handlers because we inverted the polarity :D
    
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

esp_err_t bno055_workaround(BNO055* device) {
    bno055_opmode_t currentMode = 0;
    esp_err_t res;
    res = bno055_get_mode(device, &currentMode);
    if (res != ESP_OK) return res;
    if (currentMode !=  BNO055_OPERATION_MODE_NDOF) {
        printf("!!! Reconfigure BNO055 !!! (%u != %u)\n", currentMode, BNO055_OPERATION_MODE_NDOF);
        res = bno055_set_power_mode(device, BNO055_POWER_MODE_NORMAL);
        if (res != ESP_OK) return res;

        res = bno055_set_mode(device, BNO055_OPERATION_MODE_NDOF);
        if (res != ESP_OK) return res;
    }
    return res;
}

void app_main(void) {
    esp_err_t res;
    
    res = hardware_init();
    if (res != ESP_OK) {
        printf("Failed to initialize hardware!\n");
        restart();
    }

    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), WiFi%s%s, ",
            CONFIG_IDF_TARGET,
            chip_info.cores,
            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d, ", chip_info.revision);

    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    printf("Minimum free heap size: %d bytes\n", esp_get_minimum_free_heap_size());
    
    button_init();
    
    BNO055* bno055 = get_bno055();
    
    bno055_vector_t acceleration, magnetism, orientation, rotation, linear_acceleration, gravity;
    bno055_vector_t rotation_offset;
    rotation_offset.x = 0;
    rotation_offset.y = 0;
    rotation_offset.z = 0;

    while (1) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        /*res = bno055_test(bno055);
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Testing BNO055 failed");
            continue;
        }*/
        
        res = bno055_workaround(bno055);
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Workaround failed %d\n", res);
            continue;
        }

        /*res = bno055_get_vector(bno055, BNO055_VECTOR_ACCELEROMETER, &acceleration);
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Acceleration failed to read %d\n", res);
            continue;
        }*/

        res = bno055_get_vector(bno055, BNO055_VECTOR_MAGNETOMETER, &magnetism);
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Magnetic field to read %d\n", res);
            continue;
        }

        /*res = bno055_get_vector(bno055, BNO055_VECTOR_GYROSCOPE, &orientation);
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Orientation failed to read %d\n", res);
            continue;
        }*/

        res = bno055_get_vector(bno055, BNO055_VECTOR_EULER, &rotation);
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Rotation failed to read %d\n", res);
            continue;
        }

        /*res = bno055_get_vector(bno055, BNO055_VECTOR_LINEARACCEL, &linear_acceleration);
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Linear acceleration failed to read %d\n", res);
            continue;
        }

        res = bno055_get_vector(bno055, BNO055_VECTOR_GRAVITY, &gravity);
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Gravity failed to read %d\n", res);
            continue;
        }*/
        
        if (calibrate) {
            rotation_offset.x = rotation.x;
            rotation_offset.y = rotation.y;
            rotation_offset.z = rotation.z;
            calibrate = false;
        }
        
        rotation.x -= rotation_offset.x;
        rotation.y -= rotation_offset.y;
        rotation.z -= rotation_offset.z;
        
        /*printf("\n\n");
        printf("Acceleration (m/s²)        x = %5.8f y = %5.8f z = %5.8f\n", acceleration.x, acceleration.y, acceleration.z);
        printf("Magnetic field (uT)        x = %5.8f y = %5.8f z = %5.8f\n", magnetism.x, magnetism.y, magnetism.z);
        printf("Orientation (dps)          x = %5.8f y = %5.8f z = %5.8f\n", orientation.x, orientation.y, orientation.z);
        printf("Rotation (degrees)         x = %5.8f y = %5.8f z = %5.8f\n", rotation.x, rotation.y, rotation.z);
        printf("Linear acceleration (m/s²) x = %5.8f y = %5.8f z = %5.8f\n", linear_acceleration.x, linear_acceleration.y, linear_acceleration.z);
        printf("Gravity (m/s²)             x = %5.8f y = %5.8f z = %5.8f\n", gravity.x, gravity.y, gravity.z);*/

        printf("Magnetic (uT) x: %5.4f y: %5.4f z: %5.4f  Rotation (deg): x: %5.4f y: %5.4f z: %5.4f \n", magnetism.x, magnetism.y, magnetism.z, rotation.x, rotation.y, rotation.z);
    }
}
