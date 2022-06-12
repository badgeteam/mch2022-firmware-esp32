if(CONFIG_DRIVER_FSOVERBUS_ENABLE)
    set(srcs
        "backend.c"
        "driver_fsoverbus.c"
        "filefunctions.c"
        "packetutils.c"
        "specialfunctions.c"
        "uart_backend.c"
        "uartnaive_backend.c"
    )
else()
    set(srcs "")
endif()

if(CONFIG_DRIVER_FSOVERBUS_APPFS_SUPPORT)
    list(APPEND srcs "appfsfunctions.c")
endif()

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS "include"
                       REQUIRES spi_flash)