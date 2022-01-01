#include "hardware.h"
#include <driver/spi_master.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include "managed_i2c.h"

static const char *TAG = "hardware";

PCA9555 pca9555;
BNO055 bno055;

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
    
    // BNO055 sensor on system I2C bus
    
    res = bno055_init(&bno055, I2C_BUS_SYS, BNO055_ADDR, GPIO_INT_BNO055, true);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Initializing BNO055 failed");
        return res;
    }

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

BNO055* get_bno055() {
    return &bno055;
}
