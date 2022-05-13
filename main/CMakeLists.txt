idf_component_register(
    SRCS "main.c" "menu.c" "fpga_test.c" "pax_keyboard.c" "system_wrapper.c" "appfs_wrapper.c" "graphics_wrapper.c" "settings.c" "wifi_connection.c" "rp2040_updater.c" "wifi_ota.c"
    INCLUDE_DIRS "." "include"
    EMBED_TXTFILES ${project_dir}/server_certs/isrgrootx1.pem
)
