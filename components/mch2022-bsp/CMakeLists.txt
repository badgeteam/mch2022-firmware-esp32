idf_component_register(
    SRCS "hardware.c"
    INCLUDE_DIRS "."
    REQUIRES
        "appfs"
        "bus-i2c"
        "i2c-bno055"
        "pax-graphics"
        "sdcard"
        "spi-ice40"
        "spi-ili9341"
        "mch2022-rp2040"
)
