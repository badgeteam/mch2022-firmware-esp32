#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "wifi_connection.h"

static const char *TAG = "wifi_connection";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_STARTED_BIT   BIT2

static EventGroupHandle_t wifiEventGroup;

static uint8_t retryCount = 0;
static uint8_t maxRetries = 3;
static bool isScanning = false;

#define WIFI_SORT_ERRCHECK(err) do {int res = (err); if(res) {ESP_LOGE(TAG, "WiFi connection error: %s", esp_err_to_name(res)); goto error; } } while(0)

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        xEventGroupSetBits(wifiEventGroup, WIFI_STARTED_BIT);
        if (!isScanning) {
            // Connect only if we're not scanning the WiFi.
            esp_wifi_connect();
        }
        ESP_LOGI(TAG, "WiFi station start.");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_STOP) {
        xEventGroupClearBits(wifiEventGroup, WIFI_STARTED_BIT);
        ESP_LOGI(TAG, "WiFi station stop.");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retryCount < 3) {
            esp_wifi_connect();
            retryCount++;
            ESP_LOGI(TAG, "Retrying connection");
        } else {
            ESP_LOGI(TAG, "Connection failed");
            xEventGroupSetBits(wifiEventGroup, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        retryCount = 0;
        xEventGroupSetBits(wifiEventGroup, WIFI_CONNECTED_BIT);
    }
}

void wifi_init() {
    // Create an event group for WiFi things.
    wifiEventGroup = xEventGroupCreate();
    
    // Initialise WiFi stack.
    ESP_ERROR_CHECK(esp_netif_init());
    
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Register event handlers for WiFi.
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));
    
    // Turn off WiFi hardware.
    ESP_ERROR_CHECK(esp_wifi_stop());
}

bool wifi_connect(const char* aSsid, const char* aPassword, wifi_auth_mode_t aAuthmode, uint8_t aRetryMax) {
    // Set the retry counts.
    retryCount = 0;
    maxRetries = aRetryMax;
    
    // Create a config.
    wifi_config_t wifi_config = {0};
    strncpy((char*) wifi_config.sta.ssid, aSsid, 32);
    strncpy((char*) wifi_config.sta.password, aPassword, 64);
    wifi_config.sta.threshold.authmode = aAuthmode;
    
    // Set WiFi config.
    WIFI_SORT_ERRCHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    WIFI_SORT_ERRCHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    // Disable 11b as NOC asked.
    esp_wifi_config_11b_rate(WIFI_IF_STA, true);
    // Start WiFi.
    WIFI_SORT_ERRCHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "Connecting to WiFi...");
    
    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(wifiEventGroup, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi");
        return true;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect");
        WIFI_SORT_ERRCHECK(esp_wifi_stop());
    } else {
        ESP_LOGE(TAG, "Unknown event received while waiting on connection");
        WIFI_SORT_ERRCHECK(esp_wifi_stop());
    }
    error:
    return false;
}

bool wifi_connect_ent(const char* aSsid, const char *aIdent, const char *aAnonIdent, const char* aPassword, esp_eap_ttls_phase2_types phase2, uint8_t aRetryMax) {
    retryCount = 0;
    maxRetries = aRetryMax;
    wifi_config_t wifi_config = {0};
    if (strlen(aSsid) > 32) {
        ESP_LOGE(TAG, "SSID is too long (%zu > 32)!", strlen(aSsid));
        return false;
    }
    strncpy((char*) wifi_config.sta.ssid, aSsid, 32);
    
    // Set WiFi config.
    WIFI_SORT_ERRCHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    WIFI_SORT_ERRCHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    
    // Set WPA2 ENT config.
    WIFI_SORT_ERRCHECK(esp_wifi_sta_wpa2_ent_set_identity((const uint8_t *) aAnonIdent, strlen(aAnonIdent)));
    WIFI_SORT_ERRCHECK(esp_wifi_sta_wpa2_ent_set_username((const uint8_t *) aIdent, strlen(aIdent)));
    WIFI_SORT_ERRCHECK(esp_wifi_sta_wpa2_ent_set_password((const uint8_t *) aPassword, strlen(aPassword)));
    WIFI_SORT_ERRCHECK(esp_wifi_sta_wpa2_ent_set_ttls_phase2_method(phase2));
    // Enable enterprise auth.
    WIFI_SORT_ERRCHECK(esp_wifi_sta_wpa2_ent_enable());
    // Disable 11b as NOC asked.
    WIFI_SORT_ERRCHECK(esp_wifi_config_11b_rate(WIFI_IF_STA, true));
    // Start the connection.
    WIFI_SORT_ERRCHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "Connecting to '%s' as '%s'/'%s': %s", aSsid, aIdent, aAnonIdent, aPassword);
    ESP_LOGI(TAG, "Phase2 mode: %d", phase2);
    
    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(wifiEventGroup, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi");
        return true;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect");
        WIFI_SORT_ERRCHECK(esp_wifi_stop());
    } else {
        ESP_LOGE(TAG, "Unknown event received while waiting on connection");
        WIFI_SORT_ERRCHECK(esp_wifi_stop());
    }
    error:
    return false;
}

// Shows a nice info message describing an AP record.
static inline void wifi_desc_record(wifi_ap_record_t *record) {
    // Make a string representation of BSSID.
    char *bssid_str = malloc(3*6);
    if (!bssid_str) return;
    snprintf(bssid_str, 3*6, "%02X:%02X:%02X:%02X:%02X:%02X",
        record->bssid[0], record->bssid[1], record->bssid[2],
        record->bssid[3], record->bssid[4], record->bssid[5]
    );
    
    // Make a string representation of 11b/g/n modes.
    char *phy_str = malloc(9);
    if (!phy_str) {
        free(bssid_str);
        return;
    }
    *phy_str = 0;
    if (record->phy_11b | record->phy_11g | record->phy_11n) {
        strcpy(phy_str, " 1");
    }
    if (record->phy_11b) {
        strcat(phy_str, "/b");
    }
    if (record->phy_11g) {
        strcat(phy_str, "/g");
    }
    if (record->phy_11n) {
        strcat(phy_str, "/n");
    }
    phy_str[2] = '1';
    
    ESP_LOGI(TAG, "AP %s %s rssi=%hhd%s", bssid_str, record->ssid, record->rssi, phy_str);
    free(bssid_str);
    free(phy_str);
}

// Scan for WiFi access points.
size_t wifi_scan(wifi_ap_record_t **aps_out) {
    isScanning = true;
    wifi_ap_record_t *aps = NULL;
    // Scan for any non-hidden APs on all channels.
    wifi_scan_config_t cfg = {
        .ssid    = NULL,
        .bssid   = NULL,
        .channel = 0,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = { .active={ 0, 0 } },
    };
    
    // Start the scan now.
    ESP_LOGI(TAG, "Starting scan...");
    esp_err_t res = esp_wifi_scan_start(&cfg, true);
    // Whether to call esp_wifi_stop() on finish.
    bool stopWhenDone = false;
    if (res == ESP_ERR_WIFI_NOT_STARTED) {
        // If it complains that the wifi wasn't started, then do so.
        ESP_LOGI(TAG, "Starting WiFi for scan");
        
        // Set to station but don't connect.
        res = esp_wifi_set_mode(WIFI_MODE_STA);
        if (res) goto ohno;
        
        // Start WiFi.
        res = esp_wifi_start();
        if (res) goto ohno;
        stopWhenDone = true;
        
        // Await the STA started bit.
        xEventGroupWaitBits(wifiEventGroup, WIFI_STARTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(2000));
        
        // Try again.
        res = esp_wifi_scan_start(&cfg, true);
    }
    if (res) {
        ohno:
        ESP_LOGE(TAG, "Error in WiFi scan: %s", esp_err_to_name(res));
        isScanning = false;
        return 0;
    }
    
    // Allocate memory for AP list.
    uint16_t num_ap = 0;
    WIFI_SORT_ERRCHECK(esp_wifi_scan_get_ap_num(&num_ap));
    aps = malloc(sizeof(wifi_ap_record_t) * num_ap);
    if (!aps) {
        ESP_LOGE(TAG, "Out of memory (%zd bytes)", sizeof(wifi_ap_record_t) * num_ap);
        num_ap = 0;
        esp_wifi_scan_get_ap_records(&num_ap, NULL);
        return 0;
    }
    
    // Collect APs and report findings.
    WIFI_SORT_ERRCHECK(esp_wifi_scan_get_ap_records(&num_ap, aps));
    for (uint16_t i = 0; i < num_ap; i++) {
        wifi_desc_record(&aps[i]);
    }
    
    // Clean up.
    if (aps_out) {
        // Output pointer is non-null, return the APs list.
        *aps_out = aps;
    } else {
        // Output pointer is null, free the APs list.
        free(aps);
    }
    if (stopWhenDone) {
        // Stop WiFi because it was started only for this scan.
        esp_wifi_stop();
    }
    isScanning = false;
    return num_ap;
    
    error:
    if (aps) free(aps);
    return 0;
}

// Get the strength value for a given RSSI.
wifi_strength_t wifi_rssi_to_strength(int8_t rssi) {
    if (rssi > WIFI_THRESH_VERY_GOOD) return WIFI_STRENGTH_VERY_GOOD;
    else if (rssi > WIFI_THRESH_GOOD) return WIFI_STRENGTH_GOOD;
    else if (rssi > WIFI_THRESH_BAD)  return WIFI_STRENGTH_BAD;
    else return WIFI_STRENGTH_VERY_BAD;
}
