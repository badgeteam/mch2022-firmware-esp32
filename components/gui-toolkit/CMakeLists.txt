idf_component_register(
    SRCS "menu.c"
         "gui_element_header.c"
         "graphics_wrapper.c"
         "menu.c"
    INCLUDE_DIRS "." "include"
    REQUIRES
        "pax-graphics"
        "pax-codecs"
        "pax-keyboard"
        "spi-ili9341"
        "mch2022-rp2040"
        "mch2022-bsp"
)
