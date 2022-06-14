#include "include/packetutils.h"
#include "include/specialfunctions.h"

#include <esp_sleep.h>
#include <esp_err.h>
#include <esp_log.h>

#define TAG "fsoveruart_sf"

#if CONFIG_DRIVER_FSOVERBUS_RTCMEM_SUPPORT
//This function is provided by the rtcmem driver.
esp_err_t driver_rtcmem_string_write(const char* str);

int execfile(uint8_t *data, uint16_t command, uint32_t message_id, uint32_t size, uint32_t received, uint32_t length) {
    if(received != size) return 0;

    ESP_LOGI(TAG, "Starting: %s", data);
    sendok(command, message_id);
    driver_rtcmem_string_write((char*) data);
    esp_deep_sleep(1000000);
    return 1;
}
#else
int execfile(uint8_t *data, uint16_t command, uint32_t message_id, uint32_t size, uint32_t received, uint32_t length) {
    if(received != size) return 0;

    sendns(command, message_id);
    return 1;
}
#endif

int heartbeat(uint8_t *data, uint16_t command, uint32_t message_id, uint32_t size, uint32_t received, uint32_t length) {
    if(received != size) return 0;
    sendok(command, message_id);
    return 1;
}

//Old function used in CZ20, currently unsupported. Function still exists to prevent reuse of the command.
int pythonstdin(uint8_t *data, uint16_t command, uint32_t message_id, uint32_t size, uint32_t received, uint32_t length) {
    sendok(command, message_id);
    return 1;
}

int notsupported(uint8_t *data, uint16_t command, uint32_t message_id, uint32_t size, uint32_t received, uint32_t length) {
    if(received != size) return 1;
    sendns(command, message_id);
    return 1;
}