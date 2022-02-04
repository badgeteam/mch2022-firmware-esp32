
idf_component_register(SRC_DIRS .
        REQUIRES log spi_flash bootloader_support
        INCLUDE_DIRS . )

target_compile_options(${COMPONENT_LIB} PRIVATE -DPROJECT_NAME="${CMAKE_PROJECT_NAME}")
