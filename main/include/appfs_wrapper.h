#pragma once

#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "appfs.h"
#include "ili9341.h"
#include "pax_gfx.h"

esp_err_t appfs_init(void);
void      appfs_boot_app(int fd);
void      appfs_store_app(pax_buf_t* pax_buffer, ILI9341* ili9341, const char* path, const char* name, const char* title, uint16_t version);
