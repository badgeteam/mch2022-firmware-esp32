#!/bin/bash

set -e # Exit script on error

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <port>"
    exit 1
fi

PORT=$1

echo "Burning e-fuses for MCH2022 badge on port $PORT..."

# Set the flash/psram voltage to 3.3V
espefuse.py --port $PORT --do-not-confirm set_flash_voltage 3.3V # Ignore GPIO12 (MTDI) and force flash/psram voltage (using XPD efuses)

# Write protect the basic system settings
espefuse.py --port $PORT --do-not-confirm write_protect_efuse MAC # Disables writing to MAC, MAC_CRC, CIP_VER_REV1, CHIP_VERSION, CHIP_PACKAGE fuses
espefuse.py --port $PORT --do-not-confirm write_protect_efuse CODING_SCHEME # Disables writing to CODING_SCHEME, KEY_STATUS, FLASH_CRYPT_CONFIG, BLK3_PART_RESERVE

# Write protect the XPD fuses
espefuse.py --port $PORT --do-not-confirm write_protect_efuse XPD_SDIO_FORCE # Disables writing to XPD_SDIO_FORCE, XPD_SDIO_REG and XPD_SDIO_TIEH

# Write protect the SPI pad fuses
espefuse.py --port $PORT --do-not-confirm write_protect_efuse SPI_PAD_CONFIG_CLK # Disables writing to CHIP_VER_REV2 and all SPI_PAD_CONFIG_X fuses

# Write protect the JTAG disable fuse (don't allow people to disable JTAG)
espefuse.py --port $PORT --do-not-confirm write_protect_efuse JTAG_DISABLE

# Disable the BASIC ROM console (it causes problems with waking up from deep sleep)
espefuse.py --port $PORT --do-not-confirm burn_efuse CONSOLE_DEBUG_DISABLE

# Write protect the download mode disable efuse
espefuse.py --port $PORT --do-not-confirm write_protect_efuse UART_DOWNLOAD_DIS # Disables writing to FLASH_CRYPT_CNT, UART_DOWNLOAD_DIS and EFUSE_WR_DIS_FLASH_CRYPT_CNT

# Write protect the secure boot fuses (don't allow people to enable secure boot)
espefuse.py --port $PORT --do-not-confirm write_protect_efuse DISABLE_DL_ENCRYPT # Disables writing to CONSOLE_DEBUG_DISABLE, DISABLE_DL_ENCRYPT, DISABLE_DL_DECRYPT and DISABLE_DL_CACHE
espefuse.py --port $PORT --do-not-confirm write_protect_efuse ABS_DONE_0
espefuse.py --port $PORT --do-not-confirm write_protect_efuse ABS_DONE_1
