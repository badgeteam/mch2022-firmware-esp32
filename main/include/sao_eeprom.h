#pragma once

#include <esp_system.h>
#include <stdint.h>

typedef enum _sao_type { SAO_UNFORMATTED, SAO_BINARY, SAO_JSON } sao_type_t;

typedef struct _SAO {
    sao_type_t type;
    char*      name;
    char*      driver;
    uint8_t*   driver_data;
    uint8_t    driver_data_length;
} SAO;

SAO*      sao_identify();
esp_err_t sao_free(SAO* sao);
esp_err_t sao_write_raw(size_t offset, uint8_t* buffer, size_t buffer_length);
