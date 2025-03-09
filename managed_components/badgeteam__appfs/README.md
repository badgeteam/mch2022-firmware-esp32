# ESP32 component: AppFS

This component consists of two parts: the AppFS component itself and a bootloader modification which makes it possible to launch ESP32 firmwares dynamically from the AppFS partition.

## Integrating into an ESP-IDF project
In your project directory execute the following commands:

 - cd components; git submodule add https://github.com/badgeteam/esp32-component-appfs.git; cd ..
 - mkdir bootloader_components
 - ln -s ../components/appfs appfs
 - ln -s ../components/appfs/bootloader_main main

## License
The AppFS component (appfs.c / appfs.h) is licensed as follows:

"THE BEER-WARE LICENSE" (Revision 42):
Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
this notice you can do whatever you want with this stuff. If we meet some day, 
and you think this stuff is worth it, you can buy me a beer in return. 

The bootloader overrides are licensed under Apache-2.0 license by 2015-2021 Espressif Systems (Shanghai) CO LTD.

