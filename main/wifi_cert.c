#include "wifi_cert.h"

#include <stdint.h>
#include <stdio.h>

#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_tls.h"

extern const uint8_t custom_ota_cert_pem_start[] asm("_binary_custom_ota_cert_pem_start");
extern const uint8_t custom_ota_cert_cert_pem_end[] asm("_binary_custom_ota_cert_pem_end");

extern const uint8_t isrgrootx1_pem_start[] asm("_binary_isrgrootx1_pem_start");
extern const uint8_t isrgrootx1_pem_end[] asm("_binary_isrgrootx1_pem_end");

esp_err_t init_ca_store() {
    // This function is called from main and initializes a custom certificate storage.
    // Custom certificates can be added to this storage as required.
    esp_err_t res = esp_tls_init_global_ca_store();
    if (res != ESP_OK) {
        return res;
    }

    res = esp_tls_set_global_ca_store(custom_ota_cert_pem_start, custom_ota_cert_cert_pem_end - custom_ota_cert_pem_start);  // Self-signed fallback certificate
    if (res != ESP_OK) {
        return res;
    }

    res = esp_tls_set_global_ca_store(isrgrootx1_pem_start, isrgrootx1_pem_end - isrgrootx1_pem_start);  // Let's encrypt root CA
    if (res != ESP_OK) {
        return res;
    }
    return res;
}
