#Define the name of your module here
set(mod_name "fsoverbus")
set(mod_register "fsoverbus")

if(CONFIG_DRIVER_FSOVERBUS_ENABLE)
    message(STATUS "fsoverbus enabled")
    set(EXTMODS_INIT "${EXTMODS_INIT}" "\"${mod_name}\"@\"${mod_register}\"^" CACHE INTERNAL "")
else()
    message(STATUS "fsoverbus disabled")
endif()