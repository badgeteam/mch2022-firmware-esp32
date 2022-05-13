#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_wifi.h"

void wifi_init();
bool wifi_connect(const char* aSsid, const char* aPassword, wifi_auth_mode_t aAuthmode, uint8_t aRetryMax);
