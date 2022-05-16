idf_component_register(
    SRCS "main.c"
         "appfs_wrapper.c"
         "fpga_test.c"
         "graphics_wrapper.c"
         "menu.c"
         "pax_keyboard.c"
         "rp2040_updater.c"
         "settings.c"
         "system_wrapper.c"
         "wifi_connection.c"
         "wifi_ota.c"
         "fpga_download.c"
    INCLUDE_DIRS "." "include"
    EMBED_TXTFILES ${project_dir}/server_certs/isrgrootx1.pem
)
