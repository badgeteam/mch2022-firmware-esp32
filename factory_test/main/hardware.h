#pragma once

#include <sdkconfig.h>
#include <esp_err.h>
#include <driver/spi_master.h>
#include "pca9555.h"
#include "bno055.h"
#include "ili9341.h"
#include "ice40.h"

esp_err_t hardware_init();
PCA9555* get_pca9555();
BNO055* get_bno055();
ILI9341* get_ili9341();
ICE40* get_ice40();

// Interrupts
#define GPIO_INT_STM32   0
#define GPIO_INT_PCA9555 34
#define GPIO_INT_BNO055  36
#define GPIO_INT_FPGA    39

// SD card
#define SD_PWR 5
#define SD_D0  2
#define SD_CLK 14
#define SD_CMD 15

// I2S audio
#define GPIO_I2S_CLK  14
#define GPIO_I2S_DATA 13
#define GPIO_I2S_LR   4

// System I2C bus
#define GPIO_I2C_SYS_SCL 21
#define GPIO_I2C_SYS_SDA 22
#define I2C_BUS_SYS      0
#define I2C_SPEED_SYS    20000 // 20kHz

// PCA9555 IO expander
#define PCA9555_ADDR              0x26
#define PCA9555_PIN_STM32_RESET   0
#define PCA9555_PIN_STM32_BOOT0   1
#define PCA9555_PIN_FPGA_RESET    2
#define PCA9555_PIN_FPGA_CDONE    3
#define PCA9555_PIN_BTN_START     5
#define PCA9555_PIN_BTN_SELECT    6
#define PCA9555_PIN_BTN_MENU      7
#define PCA9555_PIN_BTN_HOME      8
#define PCA9555_PIN_BTN_JOY_LEFT  9
#define PCA9555_PIN_BTN_JOY_PRESS 10
#define PCA9555_PIN_BTN_JOY_DOWN  11
#define PCA9555_PIN_BTN_JOY_UP    12
#define PCA9555_PIN_BTN_JOY_RIGHT 13
#define PCA9555_PIN_BTN_BACK      14
#define PCA9555_PIN_BTN_ACCEPT    15

// BNO055 sensor
#define BNO055_ADDR               0x28

// User I2C bus
#define GPIO_I2C_EXT_SCL 25
#define GPIO_I2C_EXT_SDA 26
#define I2C_BUS_EXT      1
#define I2C_SPEED_EXT    20000 // 20 kHz

// SPI bus
#define GPIO_SPI_CLK          18
#define GPIO_SPI_MOSI         23
#define GPIO_SPI_MISO         35
#define GPIO_SPI_CS_STM32     19
#define GPIO_SPI_CS_FPGA      27
#define GPIO_SPI_CS_LCD       32
#define GPIO_SPI_DC_LCD       33
#define SPI_BUS               VSPI_HOST
#define SPI_MAX_TRANSFER_SIZE 4094
#define SPI_DMA_CHANNEL       2
