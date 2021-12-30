#include "hardware.h"
#include <driver/spi_master.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include "managed_i2c.h"

static const char *TAG = "hardware";

PCA9555 pca9555;

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
            printf("Accept button %s\n", value ? "pressed" : "released");
            break;
        default:
            printf("Unknown button %d %s\n", pin, value ? "pressed" : "released");
    }
}

esp_err_t hardware_init() {
    esp_err_t res;
    
    // Interrupts
    res = gpio_install_isr_service(0);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Initializing ISR service failed");
        return res;
    }

    // SPI bus
    spi_bus_config_t busConfiguration = {0};
    busConfiguration.mosi_io_num     = GPIO_SPI_MOSI;
    busConfiguration.miso_io_num     = GPIO_SPI_MISO;
    busConfiguration.sclk_io_num     = GPIO_SPI_CLK;
    busConfiguration.quadwp_io_num   = -1;
    busConfiguration.quadhd_io_num   = -1;
    busConfiguration.max_transfer_sz = SPI_MAX_TRANSFER_SIZE;
    res = spi_bus_initialize(SPI_BUS, &busConfiguration, SPI_DMA_CHANNEL);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Initializing SPI bus failed");
        return res;
    }

    // System I2C bus
    res = i2c_init(I2C_BUS_SYS, GPIO_I2C_SYS_SDA, GPIO_I2C_SYS_SCL, I2C_SPEED_SYS, false, false);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Initializing system I2C bus failed");
        return res;
    }
    
    // PCA9555 IO expander on system I2C bus
    res = pca9555_init(&pca9555, I2C_BUS_SYS, PCA9555_ADDR, GPIO_INT_PCA9555);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Initializing PCA9555 failed");
        return res;
    }
    
    pca9555_set_gpio_polarity(&pca9555, PCA9555_PIN_BTN_START, true);
    pca9555_set_gpio_polarity(&pca9555, PCA9555_PIN_BTN_SELECT, true);
    pca9555_set_gpio_polarity(&pca9555, PCA9555_PIN_BTN_MENU, true);
    pca9555_set_gpio_polarity(&pca9555, PCA9555_PIN_BTN_HOME, true);
    pca9555_set_gpio_polarity(&pca9555, PCA9555_PIN_BTN_JOY_LEFT, true);
    pca9555_set_gpio_polarity(&pca9555, PCA9555_PIN_BTN_JOY_PRESS, true);
    pca9555_set_gpio_polarity(&pca9555, PCA9555_PIN_BTN_JOY_DOWN, true);
    pca9555_set_gpio_polarity(&pca9555, PCA9555_PIN_BTN_JOY_UP, true);
    pca9555_set_gpio_polarity(&pca9555, PCA9555_PIN_BTN_JOY_RIGHT, true);
    pca9555_set_gpio_polarity(&pca9555, PCA9555_PIN_BTN_BACK, true);
    pca9555_set_gpio_polarity(&pca9555, PCA9555_PIN_BTN_ACCEPT, true);
    
    pca9555.pin_state = 0; // Reset all pin states so that the interrupt function doesn't trigger all the handlers because we inverted the polarity :D
    
    pca9555_set_interrupt_handler(&pca9555, PCA9555_PIN_BTN_START, button_handler);
    pca9555_set_interrupt_handler(&pca9555, PCA9555_PIN_BTN_SELECT, button_handler);
    pca9555_set_interrupt_handler(&pca9555, PCA9555_PIN_BTN_MENU, button_handler);
    pca9555_set_interrupt_handler(&pca9555, PCA9555_PIN_BTN_HOME, button_handler);
    pca9555_set_interrupt_handler(&pca9555, PCA9555_PIN_BTN_JOY_LEFT, button_handler);
    pca9555_set_interrupt_handler(&pca9555, PCA9555_PIN_BTN_JOY_PRESS, button_handler);
    pca9555_set_interrupt_handler(&pca9555, PCA9555_PIN_BTN_JOY_DOWN, button_handler);
    pca9555_set_interrupt_handler(&pca9555, PCA9555_PIN_BTN_JOY_UP, button_handler);
    pca9555_set_interrupt_handler(&pca9555, PCA9555_PIN_BTN_JOY_RIGHT, button_handler);
    pca9555_set_interrupt_handler(&pca9555, PCA9555_PIN_BTN_BACK, button_handler);
    pca9555_set_interrupt_handler(&pca9555, PCA9555_PIN_BTN_ACCEPT, button_handler);

    // User I2C bus
    res = i2c_init(I2C_BUS_EXT, GPIO_I2C_EXT_SDA, GPIO_I2C_EXT_SCL, I2C_SPEED_EXT, false, false);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Initializing user I2C bus failed");
        return res;
    }

    return res;
}

PCA9555* get_pca9555() {
    return &pca9555;
}
