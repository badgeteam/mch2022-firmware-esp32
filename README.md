# MCH2022 ESP32 firmware: Launcher

This repository contains the ESP32 part of the firmware for the MCH2022 badge. This firmware allows for device testing, setup, OTA updates and of course launching apps.

## License

The source code contained in this repository is licensed under terms of the MIT license, more information can be found in the LICENSE file.

Source code included as submodules is licensed separately, please check the following table for details.

| Submodule                   | License                           | Author                                                                                          |
|-----------------------------|-----------------------------------|-------------------------------------------------------------------------------------------------|
| esp-idf                     | Apache License 2.0                | Espressif Systems (Shanghai) CO LTD                                                             |
| components/appfs            | THE BEER-WARE LICENSE Revision 42 | Jeroen Domburg <jeroen@spritesmods.com>                                                         |
| components/bus-i2c          | MIT                               | Nicolai Electronics                                                                             |
| components/i2c-bno055       | MIT                               | Nicolai Electronics                                                                             |
| components/mch2022-rp2040   | MIT                               | Renze Nicolai                                                                                   |
| components/pax-graphics     | MIT                               | Julian Scheffers                                                                                |
| components/pax-keyboard     | MIT                               | Julian Scheffers                                                                                |
| components/sdcard           | MIT                               | Nicolai Electronics                                                                             |
| components/spi-ice40        | MIT                               | Nicolai Electronics                                                                             |
| components/spi-ili9341      | MIT                               | Nicolai Electronics                                                                             |
| components/ws2812           | MIT                               | Unlicense / Public domain                                                                       |
| tools/[libusb-1.0.dll]      | GNU LGPL 2.1                      | See the [AUTHORS](https://github.com/libusb/libusb/blob/master/AUTHORS) document of the project |

[libusb-1.0.dll]: https://libusb.info

Some of the icons in `resources/icons` are licensed under MIT license `Copyright (c) 2019-2021 The Bootstrap Authors`. The source files for these icons can be found at https://icons.getbootstrap.com/.

The [BadgePython logo](resources/icons/python.png) may only be used as icon for the BadgePython project.

## How to build the firmware
```sh
git clone --recursive https://github.com/badgeteam/mch2022-firmware-esp32
cd mch2022-firmware-esp32
make prepare
make build
```

## How to flash and start the monitor
```sh
make flash
make monitor
```

## WebUSB tools
In [`./tools`](./tools/) you will find command line tools to push files and apps to the badge etc., and a short manual on how to use them.

## Linux permissions
Create `/etc/udev/rules.d/99-mch2022.rules` with the following contents:

```
SUBSYSTEM=="usb", ATTR{idVendor}=="16d0", ATTR{idProduct}=="0f9a", MODE="0666"
```

Then run the following commands to apply the new rule:

```
sudo udevadm control --reload-rules
sudo udevadm trigger
```

### Ubuntu snap
While we have no idea why Canonical thinks breaking things by forcing Ubuntu users to use a bad software distribution system, we do have a solution!

Install the Chromium snap package, then run the following command (in addition to adding the udev rule above). If you already have the badge connected turn it off and back on after running the command.

```
sudo snap connect chromium:raw-usb
```
