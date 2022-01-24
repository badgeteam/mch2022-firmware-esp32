/*
These functions wrap the flash read and mmap functions. The idea is that normally, they will run the real
functions. However, after appfs_wrapper_init is called with an appfs file and a flash range, any
call to these functions in that range will be redirected to the appfs functions that do the same.
The idea is that this changes the 'view' of that flash range from an (for the rest of the bootloader)
ununderstandable appfs struct mess, to one that looks the same as it would when the selected file
would be directly flashed to the partition. The nice thing here is that we can use the rest of the 
bootloader verbatim, without having to modify it.

Not we assume the ovl_start and ovl_end match the position and size of the appfs partition; we use that
if we actually boot an app.

Note that IRAM_ATTR is used here to make sure the functions that are used when/after the app loadable
segments are loaded, won't be overwritten. The IRAM_ATTR in the bootloader code dumps the function
in the loader segment instead of in random IRAM.
*/

#ifdef BOOTLOADER_BUILD

#include "appfs.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_app_format.h"
#include "soc/soc_memory_types.h"
#include "soc/soc_caps.h"
#include <string.h>
#include "soc/dport_reg.h"
#include "esp32/rom/cache.h"


static const char *TAG="appfs_wrapper";

static appfs_handle_t file_handle=APPFS_INVALID_FD;
static size_t ovl_start, ovl_size;

void appfs_wrapper_init(appfs_handle_t handle, size_t part_start, size_t part_size) {
	file_handle=handle;
	ovl_start=part_start;
	ovl_size=part_size;
}

void appfs_wrapper_deinit() {
	file_handle=APPFS_INVALID_FD;
}

//These are the actual functions.
esp_err_t __real_bootloader_flash_read(size_t src_addr, void *dest, size_t size, bool allow_decrypt);
const void *__real_bootloader_mmap(uint32_t src_addr, uint32_t size);
void __real_bootloader_munmap(const void *mapping);
void __real_bootloader_console_deinit();


static bool was_mmapped_to_appfs=false;

IRAM_ATTR const void *__wrap_bootloader_mmap(uint32_t src_addr, uint32_t size) {
	if (file_handle!=APPFS_INVALID_FD && src_addr>=ovl_start && src_addr+size<ovl_start+ovl_size) {
		ESP_LOGD(TAG, "__wrap_bootloader_mmap: redirecting map to 0x%X", src_addr);
		uint8_t *f=appfsBlMmap(file_handle);
		return &f[src_addr-ovl_start];
	} else {
		return __real_bootloader_mmap(src_addr, size);
	}
}

IRAM_ATTR void __wrap_bootloader_munmap(const void *mapping) {
	if (file_handle!=APPFS_INVALID_FD && was_mmapped_to_appfs) {
		ESP_LOGD(TAG, "__wrap_bootloader_munmap");
		appfsBlMunmap();
		was_mmapped_to_appfs=false;
	} else {
		__real_bootloader_munmap(mapping);
	}
}


IRAM_ATTR esp_err_t __wrap_bootloader_flash_read(size_t src_addr, void *dest, size_t size, bool allow_decrypt) {
	if (file_handle!=APPFS_INVALID_FD && src_addr>=ovl_start && src_addr+size<ovl_start+ovl_size) {
		ESP_LOGD(TAG, "__wrap_bootloader_flash_read: 0x%X->0x%X, %d bytes", src_addr, (int)dest, size);
		return appfs_bootloader_read(file_handle, src_addr-ovl_start, dest, size);
	} else {
		return __real_bootloader_flash_read(src_addr, dest, size, allow_decrypt);
	}
}

IRAM_ATTR static bool should_map(uint32_t load_addr) {
	return (load_addr >= SOC_IROM_LOW && load_addr < SOC_IROM_HIGH)
		   || (load_addr >= SOC_DROM_LOW && load_addr < SOC_DROM_HIGH);
}

//Note: when this is called, everything to verify and load the app has already been done *EXCEPT* the MMU
//mapping. That is done, but with wrong addresses. We need to re-do that here and then call into
//the app.
static IRAM_ATTR void mmap_and_start_app() {
	ESP_LOGD(TAG, "mmap_and_start_app()");
	//First, check if we actually need to do this. If loading the appfs app failed (e.g. because it
	//got corrupted), the previous routine will fall back to e.g. the factory app. If we would
	//adjust the MMU assuming the appfs app had loaded, we would crash.
	//Note that this is ESP32-specific.
	for (int i = 0; i < DPORT_FLASH_MMU_TABLE_SIZE; i++) {
		if (DPORT_PRO_FLASH_MMU_TABLE[i] != DPORT_FLASH_MMU_TABLE_INVALID_VAL) {
			int page=DPORT_PRO_FLASH_MMU_TABLE[i]&255;
			int addr=page*0x10000;
			if (addr<ovl_start || addr>ovl_start+ovl_size) {
				ESP_LOGI(TAG, "Not booting appfs app; not adjusting mmu.");
				return;
			}
		}
	}

	//Undo bootloader mapping. If we don't call this, the rest of the code thinks there's still
	//something mapped. Note that for now the address doesn't matter, we feed it 0.
	__real_bootloader_munmap(0);

	//Map the executable file so we can read its header.
	uint8_t *appBytes=appfsBlMmap(file_handle);
	const esp_image_header_t *hdr=(const esp_image_header_t*)appBytes;
	uint32_t entry_addr=hdr->entry_addr;

	AppfsBlRegionToMap mapRegions[8];
	int noMaps=0;
	uint8_t *pstart=appBytes+sizeof(esp_image_header_t);
	uint8_t *p=pstart;
	for (int i=0; i<hdr->segment_count; i++) {
		esp_image_segment_header_t *shdr=(esp_image_segment_header_t*)p;
		p+=sizeof(esp_image_segment_header_t);
		if (should_map(shdr->load_addr)) {
			mapRegions[noMaps].fileAddr=p-appBytes;
			mapRegions[noMaps].mapAddr=shdr->load_addr;
			mapRegions[noMaps].length=shdr->data_len;
			noMaps++;
			ESP_LOGI(TAG, "Segment %d: map to %X size %X", i, shdr->load_addr, shdr->data_len);
		} else {
			ESP_LOGI(TAG, "Segment %d: ignore (addr %X) size %X", i, shdr->load_addr, shdr->data_len);
		}
		int l=(shdr->data_len+3)&(~3);
		p+=l;
	}

	ESP_LOGD(TAG, "Unmap");
	appfsBlMunmap();
	appfsBlMapRegions(file_handle, mapRegions, noMaps);

	ESP_LOGD(TAG, "Appfs MMU adjustments done. Starting app at 0x%08x", entry_addr);
	typedef void (*entry_t)(void);
	entry_t entry = ((entry_t) entry_addr);
	(*entry)();
}


//Before the app is started, the bootloader manually sets up the cache. We can't easily intercept 
//that in order to do the transformation from fake partition offsets to appfs file contents,
//however the bootloader does have a call that it calls just before it starts up the app. We hook
//that here, manually set the cache regions to the actual app.
IRAM_ATTR void __wrap_bootloader_console_deinit() {
	if (file_handle!=APPFS_INVALID_FD) {
		mmap_and_start_app();
	}
	//Actual partition selected. Simply call the actual function.
	__real_bootloader_console_deinit();
}


//These functions are used by appfs to access the flash: these should always use unwrapped calls.
IRAM_ATTR const void* appfs_bootloader_mmap(uint32_t src_addr, uint32_t size) {
	return __real_bootloader_mmap(src_addr, size);
}

IRAM_ATTR void appfs_bootloader_munmap(const void *mapping) {
	return __real_bootloader_munmap(mapping);
}

IRAM_ATTR esp_err_t appfs_bootloader_flash_read(size_t src_addr, void *dest, size_t size, bool allow_decrypt)  {
	return __real_bootloader_flash_read(src_addr, dest, size, allow_decrypt);
}


#endif
