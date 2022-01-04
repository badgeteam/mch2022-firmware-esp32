#include <stdio.h>
#include <string.h>
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_spi_flash.h>
#include <esp_err.h>
#include <esp_log.h>
#include "hardware.h"
#include "bitstream2.h"
#include "managed_i2c.h"

static const char *TAG = "main";

bool calibrate = true;
bool display_bno_value = true;
ILI9341* ili9341 = NULL;
ICE40* ice40 = NULL;

void button_handler(uint8_t pin, bool value) {
    switch(pin) {
        case PCA9555_PIN_BTN_START: {
            printf("Start button %s\n", value ? "pressed" : "released");
            if (value) {
                esp_err_t res = ice40_load_bitstream(ice40, proto2_bin, proto2_bin_len);
                if (res != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to program the FPGA (%d)", res);
                } else {
                    printf("FPGA enabled\n");
                }
            }
            break;
        }
        case PCA9555_PIN_BTN_SELECT: {
            printf("Select button %s\n", value ? "pressed" : "released");
            if (value) {
                esp_err_t res = ice40_disable(ice40);
                if (res != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to disable the FPGA (%d)", res);
                } else {
                    printf("FPGA disabled\n");
                }
            }
            break;
        }
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
            display_bno_value = value;
            break;
        case PCA9555_PIN_BTN_ACCEPT:
            printf("Accept button %s\n", value ? "pressed" : "released");
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

void bno055_task(BNO055* bno055, bno055_vector_t* rotation_offset) {
    esp_err_t res;
    
    bno055_vector_t acceleration, magnetism, orientation, rotation, linear_acceleration, gravity;

    /*res = bno055_get_vector(bno055, BNO055_VECTOR_ACCELEROMETER, &acceleration);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Acceleration failed to read %d\n", res);
        return;
    }*/

    res = bno055_get_vector(bno055, BNO055_VECTOR_MAGNETOMETER, &magnetism);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Magnetic field to read %d\n", res);
        return;
    }

    /*res = bno055_get_vector(bno055, BNO055_VECTOR_GYROSCOPE, &orientation);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Orientation failed to read %d\n", res);
        return;
    }*/

    res = bno055_get_vector(bno055, BNO055_VECTOR_EULER, &rotation);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Rotation failed to read %d\n", res);
        return;
    }

    /*res = bno055_get_vector(bno055, BNO055_VECTOR_LINEARACCEL, &linear_acceleration);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Linear acceleration failed to read %d\n", res);
        return;
    }

    res = bno055_get_vector(bno055, BNO055_VECTOR_GRAVITY, &gravity);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Gravity failed to read %d\n", res);
        return;
    }*/
    
    if (calibrate) {
        rotation_offset->x = rotation.x;
        rotation_offset->y = rotation.y;
        rotation_offset->z = rotation.z;
        calibrate = false;
    }
    
    rotation.x -= rotation_offset->x;
    rotation.y -= rotation_offset->y;
    rotation.z -= rotation_offset->z;
    
    /*printf("\n\n");
    printf("Acceleration (m/s²)        x = %5.8f y = %5.8f z = %5.8f\n", acceleration.x, acceleration.y, acceleration.z);
    printf("Magnetic field (uT)        x = %5.8f y = %5.8f z = %5.8f\n", magnetism.x, magnetism.y, magnetism.z);
    printf("Orientation (dps)          x = %5.8f y = %5.8f z = %5.8f\n", orientation.x, orientation.y, orientation.z);
    printf("Rotation (degrees)         x = %5.8f y = %5.8f z = %5.8f\n", rotation.x, rotation.y, rotation.z);
    printf("Linear acceleration (m/s²) x = %5.8f y = %5.8f z = %5.8f\n", linear_acceleration.x, linear_acceleration.y, linear_acceleration.z);
    printf("Gravity (m/s²)             x = %5.8f y = %5.8f z = %5.8f\n", gravity.x, gravity.y, gravity.z);*/

    if (display_bno_value) {
        printf("Magnetic (uT) x: %5.4f y: %5.4f z: %5.4f  Rotation (deg): x: %5.4f y: %5.4f z: %5.4f \n", magnetism.x, magnetism.y, magnetism.z, rotation.x, rotation.y, rotation.z);
    }
}

void app_main(void) {
    esp_err_t res;
    
    res = hardware_init();
    if (res != ESP_OK) {
        printf("Failed to initialize hardware!\n");
        restart();
    }
    
    ili9341 = get_ili9341();
    ice40 = get_ice40();
    
    // LCD test
    /*uint8_t* framebuffer = heap_caps_malloc(ILI9341_BUFFER_SIZE, MALLOC_CAP_8BIT);
    if (framebuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer");
        restart();
    }
    
    memset(framebuffer, 0, ILI9341_BUFFER_SIZE); // Clear framebuffer
    res = ili9341_write(ili9341, framebuffer);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write framebuffer to LCD");
        restart();
    }

    free(framebuffer);
    */

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

    // Initialize the buttons
    button_init();
    
    // Test for connection to RP2040 and to the BNO055 over I2C
    /*BNO055* bno055 = get_bno055();

    bno055_vector_t rotation_offset;
    rotation_offset.x = 0;
    rotation_offset.y = 0;
    rotation_offset.z = 0;
    
    uint8_t data_out, data_in;
    
    enum {
        I2C_REGISTER_FW_VER,
        I2C_REGISTER_GPIO_DIR,
        I2C_REGISTER_GPIO_IN,
        I2C_REGISTER_GPIO_OUT,
        I2C_REGISTER_LCD_MODE,
        I2C_REGISTER_LCD_BACKLIGHT,
    };

    data_out = 1 << 2; // Proto 0 pin is output
    res = i2c_write_reg_n(I2C_BUS_EXT, 0x17, I2C_REGISTER_GPIO_DIR, &data_out, 1);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO direction on Pico: %d", res);
        return;
    }
    
    bool blink_state = false;
    
    while (1) {
        data_out = blink_state << 2;
        res = i2c_write_reg_n(I2C_BUS_EXT, 0x17, I2C_REGISTER_GPIO_OUT, &data_out, 1);
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set GPIO value on Pico: %d", res);
            return;
        }
        blink_state = !blink_state;

        res = i2c_read_reg(I2C_BUS_EXT, 0x17, I2C_REGISTER_GPIO_IN, &data_in, 1);
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read GPIO value from Pico %d", res);
            return;
        } else {
            printf("GPIO status: %02x\n", data_in);
        }
        bno055_task(bno055, &rotation_offset);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }*/

    // FPGA RAM passthrough test
    
    res = ice40_load_bitstream(ice40, proto2_bin, proto2_bin_len);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to program the FPGA (%d)", res);
        return;
    }
    
    uint8_t* tx_buffer = malloc(SPI_MAX_TRANSFER_SIZE);
    uint8_t* rx_buffer = malloc(SPI_MAX_TRANSFER_SIZE);
    
    const uint8_t write_cmd = 0x02;
    const uint8_t read_cmd = 0x03;
    
    uint32_t size_of_ram = 8388608;
    uint32_t position = 0;
    
    ESP_LOGI(TAG, "Writing to PSRAM...");
    int64_t tx_start_time = esp_timer_get_time();
    while (position < size_of_ram) {
        // First 4 bytes of the transmit buffer are used for CMD and 24-bit address
        tx_buffer[0] = write_cmd;
        tx_buffer[1] = (position >> 16);
        tx_buffer[2] = (position >> 8) & 0xFF;
        tx_buffer[3] = position & 0xFF;
        
        uint32_t remaining = size_of_ram - position;
        uint32_t data_length = SPI_MAX_TRANSFER_SIZE - 4;
        if (data_length > remaining) data_length = remaining;
        
        // 
        for (uint32_t index = 0; index < data_length; index++) {
            tx_buffer[index + 4] = ((position + (index)) & 0xFF); // Generate a test pattern
        }
        if (ice40_transaction(ice40, tx_buffer, data_length + 4, rx_buffer, data_length + 4) != ESP_OK) {
            ESP_LOGE(TAG, "Write transaction failed @ %u", remaining);
            return;
        }
        
        position += data_length;
    }
    int64_t tx_done_time = esp_timer_get_time();
    printf("Write took %lld microseconds\r\n", tx_done_time - tx_start_time);
    uint64_t result = (((size_of_ram) / (tx_done_time - tx_start_time))*1000*1000)/1024;
    printf("%u bytes in %lld microseconds = %llu kB/s\r\n", size_of_ram, tx_done_time - tx_start_time, result);
    
    position = 0; // Reset position
    memset(tx_buffer, 0, SPI_MAX_TRANSFER_SIZE); // Clear TX buffer

    ESP_LOGI(TAG, "Verifying PSRAM contents...");
    int64_t rx_start_time = esp_timer_get_time();
    while (position < size_of_ram) {
        tx_buffer[0] = read_cmd;
        tx_buffer[1] = (position >> 16);
        tx_buffer[2] = (position >> 8) & 0xFF;
        tx_buffer[3] = position & 0xFF;
        
        uint32_t remaining = size_of_ram - position;
        uint32_t data_length = SPI_MAX_TRANSFER_SIZE - 4;
        if (data_length > remaining) data_length = remaining;
        
        if (ice40_transaction(ice40, tx_buffer, data_length + 4, rx_buffer, data_length + 4) != ESP_OK) {
            ESP_LOGE(TAG, "Transaction failed");
            return;
        }
        
        for (uint32_t index = 0; index < data_length; index++) {
            if (rx_buffer[index + 4] != ((position + (index)) & 0xFF)) { // Verify the test pattern
                ESP_LOGE(TAG, "Verification failed @ %u + %u: %u != %u", position, index, rx_buffer[index + 4], (position + (index)) & 0xFF);
            }
        }
        
        position += data_length;
    }
    int64_t rx_done_time = esp_timer_get_time();
    printf("Read took %lld microseconds\r\n", rx_done_time - rx_start_time);
    result = (((size_of_ram) / (rx_done_time - rx_start_time))*1000*1000)/1024;
    printf("%u bytes in %lld microseconds = %llu kB/s\r\n", size_of_ram, rx_done_time - rx_start_time, result);
}
