#pragma once

#include <stdio.h>
#include <string.h>
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_system.h>
#include <esp_err.h>
#include <esp_log.h>
#include "appfs.h"

esp_err_t appfs_init(void);
uint8_t* load_file_to_ram(FILE* fd, size_t* fsize);
void appfs_boot_app(int fd);
