#pragma once

#include <esp_system.h>
#include <stdint.h>

typedef enum _sao_type { SAO_NONE, SAO_UNFORMATTED, SAO_BINARY, SAO_JSON } sao_type_t;

typedef struct _SAO {
    uint8_t type;  // sao_type_t;
    char    name[256];
    char    driver[256];
    uint8_t driver_data[256];
    uint8_t driver_data_length;
} SAO;

typedef struct __attribute__((__packed__)) _sao_binary_header {
    uint8_t magic[4];
    uint8_t name_length;
    uint8_t driver_name_length;
    uint8_t driver_data_length;
    uint8_t number_of_drivers;  // 0 indicates a single driver
} sao_binary_header_t;

// Storage driver
// Note: this driver can also be used as driver for SAOs with basic LED and button IO

#define SAO_DRIVER_STORAGE_NAME "storage"

typedef struct __attribute__((__packed__)) _sao_driver_storage_data {
    uint8_t flags;          // A combination of one or more flag bits
    uint8_t address;        // I2C address of the data EEPROM (0x50 when using main EEPROM, usually 0x51 when using a separate data EEPROM)
    uint8_t size_exp;       // For example 15 for 32 kbit
    uint8_t page_size_exp;  // For example 6 for 64 bytes
    uint8_t data_offset;    // In pages, needed to skip header
    uint8_t reserved;       // Reserved, set to 0
} sao_driver_storage_data_t;

#define SAO_DRIVER_STORAGE_FLAG_IO1_LED       (1 << 0)
#define SAO_DRIVER_STORAGE_FLAG_IO2_LED       (1 << 1)
#define SAO_DRIVER_STORAGE_FLAG_IO1_BUTTON    (1 << 2)
#define SAO_DRIVER_STORAGE_FLAG_IO2_BUTTON    (1 << 3)
#define SAO_DRIVER_STORAGE_FLAG_IO1_INTERRUPT (1 << 3)
#define SAO_DRIVER_STORAGE_FLAG_IO2_INTERRUPT (1 << 4)

// Neopixel driver

#define SAO_DRIVER_NEOPIXEL_NAME "neopixel"

enum SAO_DRIVER_NEOPIXEL_COLOR_ORDER {
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_RGB = 0,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_GBG = 1,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_GRB = 2,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_GBR = 3,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_BRG = 4,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_BGR = 5,

    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_WRGB = 6,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_WRBG = 7,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_WGRB = 8,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_WGBR = 9,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_WBRG = 10,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_WBGR = 11,

    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_RWGB = 12,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_RWBG = 13,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_RGWB = 14,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_RGBW = 15,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_RBWG = 16,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_RBGW = 17,

    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_GWRB = 18,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_GWBR = 19,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_GRWB = 20,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_GRBW = 21,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_GBWR = 22,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_GBRW = 23,

    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_BWRG = 24,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_BWGR = 25,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_BRWG = 26,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_BRGW = 27,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_BGWR = 28,
    SAO_DRIVER_NEOPIXEL_COLOR_ORDER_BGRW = 29,
};

typedef struct __attribute__((__packed__)) _sao_driver_neopixel_data {
    uint16_t length;       // Length in LEDs
    uint8_t  color_order;  // One of the values defined in the color order enum
    uint8_t  reserved;     // Reserved, set to 0
} sao_driver_neopixel_data_t;

// SSD1306 driver

#define SAO_DRIVER_SSD1306_NAME "ssd1306"

typedef struct __attribute__((__packed__)) _sao_driver_ssd1306_data {
    uint8_t address;   // I2C address of the SSD1306 OLED (usually 0x3C)
    uint8_t height;    // 32 or 64, in pixels
    uint8_t reserved;  // Reserved, set to 0
} sao_driver_ssd1306_data_t;

// NTAG NFC driver

#define SAO_DRIVER_NTAG_NAME "ntag"

typedef struct __attribute__((__packed__)) _sao_driver_ntag_data {
    uint8_t address;   // I2C address of the NTAG IC (usually 0x55)
    uint8_t size_exp;  // 10 (1k) for NT3H2111 or 11 (2k) for NT3H2211
    uint8_t reserved;  // Reserved, set to 0
} sao_driver_ntag_data_t;

// App link driver

#define SAO_DRIVER_APP_NAME "app"
// data is a string containing the slug name of the app, null terminated

void dump_eeprom_contents();
esp_err_t sao_identify(SAO* sao);
esp_err_t sao_write_raw(size_t offset, uint8_t* buffer, size_t buffer_length);
esp_err_t sao_format(const char* name, const char* driver, const uint8_t* driver_data, uint8_t driver_data_length, const char* driver2,
                     const uint8_t* driver2_data, uint8_t driver2_data_length);
