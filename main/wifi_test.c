#include "wifi_test.h"

#include <sys/socket.h>

#include "bootscreen.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hardware.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "string.h"
#include "system_wrapper.h"
#include "wifi.h"
#include "wifi_connect.h"
#include "wifi_connection.h"

static const char* wifi_auth_names[] = {
    "None", "WEP", "WPA1", "WPA2", "WPA1/2", "WPA2 Ent", "WPA3", "WPA2/3", "WAPI",
};

static const char* wifi_phase2_names[] = {
    "EAP", "MSCHAPv2", "MSCHAP", "PAP", "CHAP",
};

static void display_test_state(const char* text, const char* ssid, const char* password, wifi_auth_mode_t authmode, esp_eap_ttls_phase2_types phase2,
                               const char* username, const char* anon_ident, esp_netif_ip_info_t* ip_info, bool buttons) {
    pax_buf_t*        pax_buffer = get_pax_buffer();
    const pax_font_t* font       = pax_font_saira_regular;
    char              buffer[512];
    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0xFFFFFF);
    if (authmode == WIFI_AUTH_WPA2_ENTERPRISE) {
        snprintf(buffer, sizeof(buffer),
                 "SSID: %s\nSecurity: WPA2 Ent + %s\nIdentity: %s\nAnonymous identity: %s\nPassword: %s\nIP address: " IPSTR "\nNetmask: " IPSTR
                 "\nGateway: " IPSTR "\n",
                 ssid, wifi_phase2_names[phase2], username, anon_ident, password, IP2STR(&ip_info->ip), IP2STR(&ip_info->netmask), IP2STR(&ip_info->gw));
    } else {
        snprintf(buffer, sizeof(buffer), "SSID: %s\nSecurity: %s\nPassword: %s\nIP address: " IPSTR "\nNetmask: " IPSTR "\nGateway: " IPSTR "\n", ssid,
                 wifi_auth_names[authmode], password, IP2STR(&ip_info->ip), IP2STR(&ip_info->netmask), IP2STR(&ip_info->gw));
    }
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 5, buffer);
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 3 * 18, text);
    if (buttons) pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "🅰 test 🅱 back");
    display_flush();
}

void wifi_connection_test(xQueueHandle button_queue) {
    nvs_handle_t handle;

    nvs_open("system", NVS_READWRITE, &handle);
    char                      ssid[33]        = "<not set>";
    char                      password[65]    = "<not set>";
    char                      username[129]   = "<not set>";
    char                      anon_ident[129] = "<not set>";
    wifi_auth_mode_t          authmode        = 0;
    esp_eap_ttls_phase2_types phase2          = 0;
    size_t                    requiredSize    = 0;

    esp_err_t res = nvs_get_str(handle, "wifi.ssid", NULL, &requiredSize);
    if ((res == ESP_OK) && (requiredSize < sizeof(ssid))) {
        res = nvs_get_str(handle, "wifi.ssid", ssid, &requiredSize);
    }

    res = nvs_get_str(handle, "wifi.password", NULL, &requiredSize);
    if ((res == ESP_OK) && (requiredSize < sizeof(password))) {
        res = nvs_get_str(handle, "wifi.password", password, &requiredSize);
    }

    uint8_t dummy = 0;
    res           = nvs_get_u8(handle, "wifi.authmode", &dummy);
    authmode      = dummy;

    if (authmode == WIFI_AUTH_WPA2_ENTERPRISE) {
        res = nvs_get_str(handle, "wifi.username", NULL, &requiredSize);
        if ((res == ESP_OK) && (requiredSize < sizeof(username))) {
            res = nvs_get_str(handle, "wifi.username", username, &requiredSize);
        }
        res = nvs_get_str(handle, "wifi.anon_ident", NULL, &requiredSize);
        if ((res == ESP_OK) && (requiredSize < sizeof(anon_ident))) {
            res = nvs_get_str(handle, "wifi.anon_ident", anon_ident, &requiredSize);
        }

        dummy  = 0;
        res    = nvs_get_u8(handle, "wifi.phase2", &dummy);
        phase2 = dummy;
    }

    nvs_close(handle);

    bool quit             = false;
    char test_result[128] = {0};
    while (!quit) {
        esp_netif_ip_info_t* ip_info = wifi_get_ip_info();
        wifi_disconnect_and_disable();
        display_test_state(test_result, ssid, password, authmode, phase2, username, anon_ident, ip_info, true);
        quit = !wait_for_button();
        if (quit) break;
        display_test_state("Connecting...", ssid, password, authmode, phase2, username, anon_ident, ip_info, false);

        if (!wifi_connect_to_stored()) {
            sprintf(test_result, "Failed to connect to network!");
            continue;
        }

        display_test_state("Testing...", ssid, password, authmode, phase2, username, anon_ident, ip_info, false);

        esp_wifi_set_ps(WIFI_PS_NONE);  // Disable any WiFi power save mode

        esp_http_client_config_t config = {.url = "https://mch2022.ota.bodge.team/test.bin", .use_global_ca_store = true, .keep_alive_enable = true};

        esp_http_client_handle_t client     = esp_http_client_init(&config);
        int64_t                  time_start = esp_timer_get_time();
        esp_err_t                err        = esp_http_client_perform(client);
        int64_t                  time_end   = esp_timer_get_time();

        if (err != ESP_OK) {
            snprintf(test_result, sizeof(test_result), "Failed: %s", esp_err_to_name(err));
            continue;
        }

        int status_code = esp_http_client_get_status_code(client);

        if (status_code != 200) {
            snprintf(test_result, sizeof(test_result), "Failed: received status code %u", status_code);
            continue;
        }

        float content_length = ((esp_http_client_get_content_length(client) * 8) / 1024.0) / 1024.0;  // megabit

        if (content_length < 1) {
            snprintf(test_result, sizeof(test_result), "Failed: received no data");
        }

        float time_diff = (time_end - time_start) / 1000000.0;  // seconds

        float speed = content_length / time_diff;  // Mbps

        printf("Downloaded %.2f megabit in %.2f seconds: %.2f Mbps\n", content_length, time_diff, speed);

        snprintf(test_result, sizeof(test_result), "Success! Speed: %.2f Mbps", speed);
    }
}
