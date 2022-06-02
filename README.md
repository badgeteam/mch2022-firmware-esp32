# MCH2022 ESP32 firmware: Launcher

This repository contains the ESP32 part of the firmware for the MCH2022 badge. This firmware allows for device testing, setup, OTA updates and of course launching apps.

## License

The source code contained in this repository is licensed under terms of the MIT license, more information can be found in the LICENSE file.

Source code included as submodules is licensed separately, please check the following table for details.

| Submodule                   | License                           | Author                                                 |
|-----------------------------|-----------------------------------|--------------------------------------------------------|
| esp-idf                     | Apache License 2.0                | Espressif Systems (Shanghai) CO LTD                    |
| components/appfs            | THE BEER-WARE LICENSE Revision 42 | Jeroen Domburg <jeroen@spritesmods.com>                |
| components/bus-i2c          | MIT                               | Nicolai Electronics                                    |
| components/i2c-bno055       | MIT                               | Nicolai Electronics                                    |
| components/mch2022-rp2040   | MIT                               | Renze Nicolai                                          |
| components/pax-graphics     | MIT                               | Julian Scheffers                                       |
| components/pax-keyboard     | MIT                               | Julian Scheffers                                       |
| components/sdcard           | MIT                               | Nicolai Electronics                                    |
| components/spi-ice40        | MIT                               | Nicolai Electronics                                    |
| components/spi-ili9341      | MIT                               | Nicolai Electronics                                    |
| components/ws2812           | MIT                               | Unlicense / Public domain                              |

## How to make

git clone --recursive https://github.com/badgeteam/mch2022-firmware-esp32
cd mch2022-firmware-esp32
make prepare
make
