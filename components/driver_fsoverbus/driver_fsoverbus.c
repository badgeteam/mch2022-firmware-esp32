#include <sdkconfig.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <esp_err.h>
#include <esp_log.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_vfs.h>
#include <dirent.h>
#include <esp_intr_alloc.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

#include "include/driver_fsoverbus.h"
#include "include/filefunctions.h"
#include "include/packetutils.h"
#include "include/specialfunctions.h"
#include "include/fsob_backend.h"
#include "include/appfsfunctions.h"
#include "include/functions.h"

#define TAG "fsob"
#define min(a,b) (((a) < (b)) ? (a) : (b))
#define CACHE_SIZE (2048)

TimerHandle_t timeout;

uint8_t command_in[CACHE_SIZE];
void fsob_timeout_function( TimerHandle_t xTimer );


//Function lookup tables

int (*specialfunction[SPECIALFUNCTIONSLEN])(uint8_t *data, uint16_t command, uint32_t message_id, uint32_t size, uint32_t received, uint32_t length);
int (*filefunction[FILEFUNCTIONSLEN])(uint8_t *data, uint16_t command, uint32_t message_id, uint32_t size, uint32_t received, uint32_t length);

void handleFSCommand(uint8_t *data, uint16_t command, uint32_t message_id, uint32_t size, uint32_t received, uint32_t length) {
    static uint32_t write_pos;
    if(received == length) { //First data of the packet
        write_pos = 0;
    }
    uint8_t *buffer = command_in;
    
    if(length > CACHE_SIZE){  //Incoming buffer exceeds local cache, directly use buffer instead of copying
        buffer = data;
    } else if(length > 0) {
        memcpy(&command_in[write_pos], data, length);
        write_pos += length;
    }

    int return_val = 0;
    if(command < FILEFUNCTIONSBASE) {
        if(command < SPECIALFUNCTIONSLEN) {
            return_val = specialfunction[command](buffer, command, message_id, size, received, length);
        }
    } else if(command < BADGEFUNCTIONSBASE) {
        if((command-FILEFUNCTIONSBASE) < FILEFUNCTIONSLEN) {
            return_val = filefunction[command-FILEFUNCTIONSBASE](buffer, command, message_id, size, received, length);
        }
    }
    if(return_val) {    //Function has indicated that next payload should write at start of buffer.
        write_pos = 0;
    }
}

void fsob_timeout_function( TimerHandle_t xTimer ) {
    ESP_LOGI(TAG, "Saw no message for 1s assuming task crashed. Resetting...");
    fsob_reset();
}

void fsob_stop_timeout() {
     xTimerStop(timeout, 1);
}

void fsob_start_timeout() {
    xTimerStart(timeout, 1);
}

esp_err_t driver_fsoverbus_init(void) { 
    specialfunction[EXECFILE] = execfile;
    specialfunction[HEARTBEAT] = heartbeat;
    specialfunction[PYTHONSTDIN] = pythonstdin;
    
    filefunction[GETDIR] = getdir;
    filefunction[READFILE] = readfile;
    filefunction[WRITEFILE] = writefile;
    filefunction[DELFILE] = delfile;
    filefunction[DUPLFILE] = duplfile;
    filefunction[MVFILE] = mvfile;
    filefunction[MAKEDIR] = makedir;

    #if CONFIG_DRIVER_FSOVERBUS_APPFS_SUPPORT
    specialfunction[APPFSBOOT] = appfsboot;
    filefunction[APPFSDIR] = appfslist;
    filefunction[APPFSDEL] = appfsdel;
    filefunction[APPFSWRITE] = appfswrite;
    #else
    specialfunction[APPFSBOOT] = notsupported;
    filefunction[APPFSDIR] = notsupported;
    filefunction[APPFSDEL] = notsupported;
    filefunction[APPFSWRITE] = notsupported;
    #endif
        
    fsob_init();

    ESP_LOGI(TAG, "fs over bus registered.");
    
    timeout = xTimerCreate("FSoverBUS_timeout", 100, false, 0, fsob_timeout_function);
    return ESP_OK;
} 