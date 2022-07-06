#ifndef DRIVER_FSOVERUART_H
#define DRIVER_FSOVERUART_H

#include <stdint.h>
#include <esp_err.h>

typedef void (*fsob_log_fn_t)(char*);

esp_err_t driver_fsoverbus_init(fsob_log_fn_t log_fn);

void fsob_log(char* fmt, ...);
void handleFSCommand(uint8_t *data, uint16_t command, uint32_t message_id, uint32_t size, uint32_t received, uint32_t length);
void fsob_start_timeout();
void fsob_stop_timeout();
void fsob_receive_bytes(uint8_t *data, size_t len);

#endif
