/*
 * SPDX-FileCopyrightText: 2017-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"
#include "esp_efuse.h"
#include <assert.h>
#include "esp_efuse_custom_table.h"

// md5_digest_table 6db56af37887e1bd4dd25861bbc384d1
// This file was generated from the file esp_efuse_custom_table.csv. DO NOT CHANGE THIS FILE MANUALLY.
// If you want to change some fields, you need to change esp_efuse_custom_table.csv file
// then run `efuse_common_table` or `efuse_custom_table` command it will generate this file.
// To show efuse_table run the command 'show_efuse_table'.

#define MAX_BLK_LEN CONFIG_EFUSE_MAX_BLK_LEN

// The last free bit in the block is counted over the entire file.


static const esp_efuse_desc_t WR_DIS_WR_DIS[] = {
    {EFUSE_BLK0, 1, 1}, 	 // Write protection for WR_DIS,
};

static const esp_efuse_desc_t WR_DIS_MAC_AND_CHIP_INFO[] = {
    {EFUSE_BLK0, 3, 1}, 	 // Write protection for MAC,
};

static const esp_efuse_desc_t WR_DIS_XPD[] = {
    {EFUSE_BLK0, 5, 1}, 	 // Write protection for XPD_SDIO_FORCE,
};

static const esp_efuse_desc_t WR_DIS_SPI_PAD[] = {
    {EFUSE_BLK0, 6, 1}, 	 // Write protection for CHIP_VER_REV2,
};

static const esp_efuse_desc_t WR_DIS_SCHEME_KEY_CRYPT[] = {
    {EFUSE_BLK0, 10, 1}, 	 // Write protection for CODING_SCHEME,
};

static const esp_efuse_desc_t WR_DIS_ABS_DONE_0[] = {
    {EFUSE_BLK0, 12, 1}, 	 // Write protection for ABS_DONE_0,
};

static const esp_efuse_desc_t WR_DIS_ABS_DONE_1[] = {
    {EFUSE_BLK0, 13, 1}, 	 // Write protection for ABS_DONE_1,
};

static const esp_efuse_desc_t WR_DIS_JTAG_DISABLE[] = {
    {EFUSE_BLK0, 14, 1}, 	 // Write protection for JTAG_DISABLE,
};

static const esp_efuse_desc_t WR_DIS_CONSOLE_DEBUG_AND_DISABLE_DL_CRYPT[] = {
    {EFUSE_BLK0, 15, 1}, 	 // Write protection for CONSOLE_DEBUG_DISABLE,
};





const esp_efuse_desc_t* ESP_EFUSE_WR_DIS_WR_DIS[] = {
    &WR_DIS_WR_DIS[0],    		// Write protection for WR_DIS
    NULL
};

const esp_efuse_desc_t* ESP_EFUSE_WR_DIS_MAC_AND_CHIP_INFO[] = {
    &WR_DIS_MAC_AND_CHIP_INFO[0],    		// Write protection for MAC
    NULL
};

const esp_efuse_desc_t* ESP_EFUSE_WR_DIS_XPD[] = {
    &WR_DIS_XPD[0],    		// Write protection for XPD_SDIO_FORCE
    NULL
};

const esp_efuse_desc_t* ESP_EFUSE_WR_DIS_SPI_PAD[] = {
    &WR_DIS_SPI_PAD[0],    		// Write protection for CHIP_VER_REV2
    NULL
};

const esp_efuse_desc_t* ESP_EFUSE_WR_DIS_SCHEME_KEY_CRYPT[] = {
    &WR_DIS_SCHEME_KEY_CRYPT[0],    		// Write protection for CODING_SCHEME
    NULL
};

const esp_efuse_desc_t* ESP_EFUSE_WR_DIS_ABS_DONE_0[] = {
    &WR_DIS_ABS_DONE_0[0],    		// Write protection for ABS_DONE_0
    NULL
};

const esp_efuse_desc_t* ESP_EFUSE_WR_DIS_ABS_DONE_1[] = {
    &WR_DIS_ABS_DONE_1[0],    		// Write protection for ABS_DONE_1
    NULL
};

const esp_efuse_desc_t* ESP_EFUSE_WR_DIS_JTAG_DISABLE[] = {
    &WR_DIS_JTAG_DISABLE[0],    		// Write protection for JTAG_DISABLE
    NULL
};

const esp_efuse_desc_t* ESP_EFUSE_WR_DIS_CONSOLE_DEBUG_AND_DISABLE_DL_CRYPT[] = {
    &WR_DIS_CONSOLE_DEBUG_AND_DISABLE_DL_CRYPT[0],    		// Write protection for CONSOLE_DEBUG_DISABLE
    NULL
};
