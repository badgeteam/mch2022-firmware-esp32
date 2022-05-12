idf_component_register(
    SRCS         "esp_efuse_custom_table.c" "efuse.c"
    INCLUDE_DIRS "."
                 "include"
    REQUIRES     "spi_flash"
)
