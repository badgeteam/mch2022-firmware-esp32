idf_component_register(
    SRCS "main.c"
         "appfs_wrapper.c"
         "fpga_test.c"
         "graphics_wrapper.c"
         "menu.c"
         "rp2040_updater.c"
         "settings.c"
         "system_wrapper.c"
         "wifi_connection.c"
         "wifi_connect.c"
         "wifi_ota.c"
         "fpga_download.c"
         "audio.c"
         "bootscreen.c"
         "menus/launcher.c"
         "menus/settings.c"
         "menus/start.c"
         "menus/dev.c"
         "menus/wifi.c"
         "uninstall.c"
         "file_browser.c"
         "test_common.c"
         "factory_test.c"
    INCLUDE_DIRS "."
                 "include"
                 "menus"
    EMBED_TXTFILES ${project_dir}/resources/isrgrootx1.pem
    EMBED_FILES ${project_dir}/resources/wallpaper.png
                ${project_dir}/resources/fpga_selftest.bin
                ${project_dir}/resources/rp2040_firmware.bin
                ${project_dir}/resources/boot.snd
                ${project_dir}/resources/mch2022_logo.png
                ${project_dir}/resources/logo_screen.png
)
