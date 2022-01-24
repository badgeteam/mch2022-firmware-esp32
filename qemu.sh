#!/usr/bin/env bash

set -e
set -u

export IDF_PATH="$PWD/esp-idf"
export IDF_EXPORT_QUIET=0
source "$IDF_PATH"/export.sh

cd build

# Create an empty file, 16MB in size
dd if=/dev/zero bs=1M count=16 of=flash.bin

# Copy the bootloader into the file
dd if=bootloader/bootloader.bin bs=1 seek=$((0x1000)) of=flash.bin conv=notrunc

# Copy the partition table into the file
dd if=partition_table/partition-table.bin bs=1 seek=$((0x8000)) of=flash.bin conv=notrunc

# Copy the firmware into the file
dd if=main.bin bs=1 seek=$((0x10000)) of=flash.bin conv=notrunc

# Run QEMU
qemu-system-xtensa -nographic -machine esp32 -drive 'file=flash.bin,if=mtd,format=raw'
