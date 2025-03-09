#!/usr/bin/env bash
esptool.py --port /dev/ttyACM0 --baud 960000 write_flash 0x330000 appfs.bin
