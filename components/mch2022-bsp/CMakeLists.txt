idf_component_register(
    SRCS "hardware.c" "rp2040.c"
    INCLUDE_DIRS "."
    REQUIRES
        "appfs"
        "bus-i2c"
        "i2c-bno055"
        "pax-graphics"
        "renze-graphics"
        "sdcard"
        "spi-ice40"
        "spi-ili9341"
)
