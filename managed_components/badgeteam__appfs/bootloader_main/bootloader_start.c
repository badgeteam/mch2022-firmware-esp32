/*
 * SPDX-FileCopyrightText: 2015-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include "appfs.h"
#include "appfs_flashfunctions_wrapper.h"
#include "bootloader_common.h"
#include "bootloader_flash_priv.h"
#include "bootloader_init.h"
#include "bootloader_utility.h"
#include "esp_log.h"
#include "sdkconfig.h"
#if CONFIG_APPFS_USE_RTC_REG
#include "soc/rtc_cntl_reg.h"
#endif

static const char* TAG = "bootloader";

// Copied from kchal, which we don't want to link inhere.
// See 8bkc-hal/kchal.c for explanation of the bits in the store0 register
static int appfs_get_new_app() {
#if CONFIG_APPFS_USE_RTC_REG
    // Use RTC reg as FD storage.
    uint32_t r = REG_READ(RTC_CNTL_STORE0_REG);
    ESP_LOGI(TAG, "RTC store0 reg: %x", r);
    if ((r & 0xFF000000) != 0xA5000000) return -1;
    return r & 0xff;
#else
    // Obtain pointer to retained memory.
    rtc_retain_mem_t* mem = bootloader_common_get_rtc_retain_mem();
    appfs_bootsel_t* bootsel = (appfs_bootsel_t*)&mem->custom;
    // Detect bootsel presence.
    if (bootsel->magic != APPFS_BOOTSEL_MAGIC) {
        return -1;
    }
    // Obtain FD from retained memory.
    return bootsel->fd;
#endif
}

// Find the position/size of the appfs partition
static bool find_appfs_part(size_t* pos, size_t* len) {
    const esp_partition_info_t* partitions = bootloader_mmap(ESP_PARTITION_TABLE_OFFSET, ESP_PARTITION_TABLE_MAX_LEN);
    if (!partitions) {
        ESP_LOGE(TAG, "bootloader_mmap(0x%x, 0x%x) failed", ESP_PARTITION_TABLE_OFFSET, ESP_PARTITION_TABLE_MAX_LEN);
        return false;
    }
    ESP_LOGD(TAG, "mapped partition table 0x%x at 0x%x", ESP_PARTITION_TABLE_OFFSET, (intptr_t)partitions);

    int num_partitions;
    esp_err_t err = esp_partition_table_verify(partitions, true, &num_partitions);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to verify partition table");
        return false;
    }

    bool found = false;
    for (int i = 0; i < num_partitions; i++) {
        const esp_partition_info_t* partition = &partitions[i];
        if (partition->type == APPFS_PART_TYPE && partition->subtype == APPFS_PART_SUBTYPE) {
            *pos = partition->pos.offset;
            *len = partition->pos.size;
            found = true;
        }
    }

    bootloader_munmap(partitions);
    return found;
}

/*
 * We arrive here after the ROM bootloader finished loading this second stage bootloader from flash.
 * The hardware is mostly uninitialized, flash cache is down and the app CPU is in reset.
 * We do have a stack, so we can do the initialization in C.
 */
void __attribute__((noreturn)) call_start_cpu0(void) {
    // Hardware initialization
    if (bootloader_init() != ESP_OK) {
        bootloader_reset();
    }

    int boot_index = INVALID_INDEX;
    bootloader_state_t bs = {0};
    if (!bootloader_utility_load_partition_table(&bs)) {
        ESP_LOGE(TAG, "load partition table error!");
        bootloader_reset();
    }

    size_t appfs_pos, appfs_len;
    if (!find_appfs_part(&appfs_pos, &appfs_len)) {
        ESP_LOGE(TAG, "No appfs found!");
        goto error;
    }

    // We have an appfs
    ESP_LOGI(TAG, "AppFs found @ offset 0x%X", appfs_pos);
    esp_err_t err = appfsBlInit(appfs_pos, appfs_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AppFs initialization failed");
        appfsBlDeinit();
        goto error;
    }
    ESP_LOGI(TAG, "AppFs initialized");

    appfs_handle_t handle = appfs_get_new_app();

    if (handle == APPFS_INVALID_FD) {
        ESP_LOGW(TAG, "No AppFS app selected or failed to open app, starting launcher (%d)!", handle);
        appfsBlDeinit();
        goto error;
    }

    ESP_LOGI(TAG, "Wrapping flash functions and booting app...");
    appfs_wrapper_init(handle, appfs_pos, appfs_len);
    // De-init the high-level parts of appfs. Reading/mmap'ping a file handle still is explicitly
    // allowed after this, though.
    appfsBlDeinit();
    // Note that the rest of the bootloader code has no clue about appfs, and as such won't try
    // to boot it. We 'fix' that by chucking the appfs partition (which is now wrapped so the rest
    // of the bootloader reads from the selected file when it thinks it loads from the app) into
    // the top OTA slot.
    bs.ota[0].offset = appfs_pos;
    bs.ota[0].size = appfs_len;
    bs.app_count = 1;
    // And bingo bango, we can now boot from appfs as if it is the first ota partition.
#if CONFIG_APPFS_USE_RTC_REG
    // Store magic to prevent bootloader from starting app again while keeping app fd in memory
    REG_WRITE(RTC_CNTL_STORE0_REG, 0xA6000000 | handle);
#else
    // Mark bootsel as invalid to prevent repeated start.
    rtc_retain_mem_t* mem = bootloader_common_get_rtc_retain_mem();
    appfs_bootsel_t* bootsel = (appfs_bootsel_t*)&mem->custom;
    bootsel->valid = false;
#endif
    bootloader_utility_load_boot_image(&bs, 0);
    // Still here? Must be an error.
error:
    // Try to fallback to factory part
    // bootloader_utility_load_boot_image(&bs, -1);

    // Select the number of boot partition
    boot_index = bootloader_utility_get_selected_boot_partition(&bs);
    if (boot_index == INVALID_INDEX) {
        bootloader_reset();
    }

    // 3. Load the app image for booting
    bootloader_utility_load_boot_image(&bs, boot_index);

    ESP_LOGE(TAG, "Bootloader end");
    bootloader_reset();
}

// Return global reent struct if any newlib functions are linked to bootloader
struct _reent* __getreent(void) {
    return _GLOBAL_REENT;
}
