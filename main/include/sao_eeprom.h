#pragma once

#include <esp_system.h>
#include <stdint.h>

typedef enum _sao_type { SAO_NONE, SAO_UNFORMATTED, SAO_BINARY, SAO_JSON } sao_type_t;

typedef struct _SAO {
    uint8_t type;  // sao_type_t;
    char    name[256];
    char    driver[256];
    uint8_t driver_data[256];
    uint8_t driver_data_length;
} SAO;

esp_err_t sao_identify(SAO* sao);
esp_err_t sao_write_raw(size_t offset, uint8_t* buffer, size_t buffer_length);
