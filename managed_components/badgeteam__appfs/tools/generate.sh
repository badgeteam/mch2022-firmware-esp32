#!/usr/bin/env bash

# Usage example

python appfs_generate.py 8192000 appfs.bin
python appfs_add_file.py appfs.bin ../Apps/badgePython/build/badge_firmware.bin python "BadgePython" 1
python appfs_add_file.py appfs.bin ../Apps/ESP32\ binaries/gnuboy.bin gnuboy "GNUBOY Game Boy emulator" 1
