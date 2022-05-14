#pragma once

#include <sdkconfig.h>
#include <esp_err.h>
#include <driver/spi_master.h>
#include "bno055.h"
#include "ili9341.h"
#include "ice40.h"
#include "rp2040.h"

// Interrupts
#define GPIO_INT_RP2040  34
#define GPIO_INT_BNO055  36
#define GPIO_INT_FPGA    39

// SD card
#define SD_PWR 19 // Also LED power
#define SD_D0  2
#define SD_CLK 14
#define SD_CMD 15

// LEDs
#define GPIO_LED_DATA 5

// I2S audio
#define GPIO_I2S_CLK  14
#define GPIO_I2S_DATA 13
#define GPIO_I2S_LR   4

// I2C bus
#define GPIO_I2C_SYS_SCL 21
#define GPIO_I2C_SYS_SDA 22
#define I2C_BUS_SYS      0
#define I2C_SPEED_SYS    8000 // 8 kHz //20000 // 20 kHz

// RP2040 co-processor
#define RP2040_ADDR 0x17

// BNO055 sensor
#define BNO055_ADDR 0x28

// SPI bus
#define GPIO_SPI_CLK          18
#define GPIO_SPI_MOSI         23
#define GPIO_SPI_MISO         35
#define GPIO_SPI_CS_RP2040    19
#define GPIO_SPI_CS_FPGA      27
#define SPI_BUS               VSPI_HOST
#define SPI_MAX_TRANSFER_SIZE 4094
#define SPI_DMA_CHANNEL       2

// LCD display
#define GPIO_LCD_RESET        25
#define GPIO_LCD_MODE         26
#define GPIO_SPI_CS_LCD       32
#define GPIO_SPI_DC_LCD       33

esp_err_t bsp_init();
esp_err_t bsp_rp2040_init();
esp_err_t bsp_ice40_init();
esp_err_t bsp_bno055_init();

ILI9341* get_ili9341();
RP2040* get_rp2040();
ICE40* get_ice40();
BNO055* get_bno055();
