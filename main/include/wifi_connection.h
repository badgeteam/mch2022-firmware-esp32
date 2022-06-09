#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_wpa2.h"

// Simpler interpretation of WiFi signal strength.
typedef enum {
	WIFI_STRENGTH_VERY_BAD,
	WIFI_STRENGTH_BAD,
	WIFI_STRENGTH_GOOD,
	WIFI_STRENGTH_VERY_GOOD,
} wifi_strength_t;

// Thresholds for aforementioned signal strength definitions.
#define WIFI_THRESH_BAD       -80
#define WIFI_THRESH_GOOD      -70
#define WIFI_THRESH_VERY_GOOD -67

// Firt time initialisation of the WiFi stack.
void wifi_init();

// Connect to a traditional username/password WiFi network.
bool wifi_connect(const char* aSsid, const char* aPassword, wifi_auth_mode_t aAuthmode, uint8_t aRetryMax);

// Connect to a WPA2 enterprise WiFi network.
bool wifi_connect_ent(const char* aSsid, const char *aIdent, const char *aAnonIdent, const char* aPassword, esp_eap_ttls_phase2_types phase2, uint8_t aRetryMax);

// Scan for WiFi networks.
// Updates the APs pointer if non-null.
// Returns the number of APs found.
size_t wifi_scan(wifi_ap_record_t **aps);

// Get the strength value for a given RSSI.
wifi_strength_t wifi_rssi_to_strength(int8_t rssi);
