/*
 * SPDX-FileCopyrightText: 2015-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef BOOTLOADER_BUILD

#include <stdbool.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "bootloader_init.h"
#include "bootloader_utility.h"
#include "bootloader_common.h"
#include "appfs.h"
#include "bootloader_flash_priv.h"
#include "appfs_flashfunctions_wrapper.h"

static const char *TAG="bootloader";

//Copied from kchal, which we don't want to link inhere.
//See 8bkc-hal/kchal.c for explanation of the bits in the store0 register
static int appfs_get_new_app() {
	uint32_t r=REG_READ(RTC_CNTL_STORE0_REG);
	ESP_LOGI(TAG, "RTC store0 reg: %x", r);
	if ((r&0xFF000000)!=0xA5000000) return -1;
	return r&0xff;
}

//Find the position/size of the appfs partition
static bool find_appfs_part(size_t *pos, size_t *len) {
	const esp_partition_info_t *partitions = bootloader_mmap(ESP_PARTITION_TABLE_OFFSET, ESP_PARTITION_TABLE_MAX_LEN);
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

	bool found=false;
	for (int i = 0; i < num_partitions; i++) {
		const esp_partition_info_t *partition = &partitions[i];
		if (partition->type==APPFS_PART_TYPE && partition->subtype==APPFS_PART_SUBTYPE) {
			*pos=partition->pos.offset;
			*len=partition->pos.size;
			found=true;
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
void __attribute__((noreturn)) call_start_cpu0(void)
{
	// Hardware initialization
	if (bootloader_init() != ESP_OK) {
		bootloader_reset();
	}

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

	//We have an appfs
	ESP_LOGI(TAG, "AppFs found @ offset 0x%X", appfs_pos);
	esp_err_t err=appfsBlInit(appfs_pos, appfs_len);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "AppFs initialization failed");
		appfsBlDeinit();
		goto error;
	}
	ESP_LOGI(TAG, "AppFs initialized");
	
	int app=appfs_get_new_app();
	appfs_handle_t handle=0;
	if (app<0) {
		//Load default app
		handle=appfsOpen("chooser.app");
	} else {
		handle=app;
	}
	if (handle==APPFS_INVALID_FD) {
		ESP_LOGE(TAG, "Couldn't open app (%d)!", app);
		appfsBlDeinit();
		goto error;
	}

	ESP_LOGI(TAG, "Wrapping flash functions and booting app...");
	appfs_wrapper_init(handle, appfs_pos, appfs_len);
	//De-init the high-level parts of appfs. Reading/mmap'ping a file handle still is explicitly
	//allowed after this, though.
	appfsBlDeinit();
	//Note that the rest of the bootloader code has no clue about appfs, and as such won't try
	//to boot it. We 'fix' that by chucking the appfs partition (which is now wrapped so the rest
	//of the bootloader reads from the selected file when it thinks it loads from the app) into
	//the top OTA slot.
	bs.ota[0].offset=appfs_pos;
	bs.ota[0].size=appfs_len;
	bs.app_count=1;
	//And bingo bango, we can now boot from appfs as if it is the first ota partition.
	bootloader_utility_load_boot_image(&bs, 0);
	//Still here? Must be an error.
error:
	//Try to fallback to factory part
	bootloader_utility_load_boot_image(&bs, -1);

	ESP_LOGE(TAG, "Bootloader end");
	bootloader_reset();
}



// Return global reent struct if any newlib functions are linked to bootloader
struct _reent *__getreent(void)
{
	return _GLOBAL_REENT;
}

#endif
