#pragma once

#include <stdbool.h>

#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_wpa2.h"

// Camp WiFi settings.
#define WIFI_MCH2022_SSID     "MCH2022"
#define WIFI_MCH2022_USER     "badge"
#define WIFI_MCH2022_IDENT    "badge"
#define WIFI_MCH2022_PASSWORD "badge"
#define WIFI_MCH2022_AUTH     WIFI_AUTH_WPA2_ENTERPRISE
#define WIFI_MCH2022_PHASE2   ESP_EAP_TTLS_PHASE2_PAP

bool wifi_set_defaults();
bool wifi_check_configured();
