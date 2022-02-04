idf_component_register(SRCS "bootloader_start.c" "appfs_flashfunctions_wrapper.c"
                    REQUIRES bootloader bootloader_support appfs)

idf_build_get_property(target IDF_TARGET)
# Use the linker script files from the actual bootloader
set(scripts "${IDF_PATH}/components/bootloader/subproject/main/ld/${target}/bootloader.ld"
            "${IDF_PATH}/components/bootloader/subproject/main/ld/${target}/bootloader.rom.ld")

target_linker_script(${COMPONENT_LIB} INTERFACE "${scripts}")

target_link_libraries(${COMPONENT_LIB} INTERFACE 
        "-Wl,--wrap=bootloader_flash_read"
        "-Wl,--wrap=bootloader_mmap"
        "-Wl,--wrap=bootloader_munmap"
        "-Wl,--wrap=bootloader_console_deinit"
        )

