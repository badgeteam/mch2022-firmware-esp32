#include "wifi_test.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "string.h"
#include "esp_crt_bundle.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <sys/socket.h>
#include "esp_wifi.h"
#include "bootscreen.h"
#include "wifi.h"
#include "wifi_connect.h"

#define MAX_HTTP_OUTPUT_BUFFER 2048
static const char *TAG = "WiFi test";

extern const uint8_t server_cert_pem_start[] asm("_binary_isrgrootx1_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_isrgrootx1_pem_end");

esp_err_t _test_http_event_handler(esp_http_client_event_t *evt)
{
    static int output_len;
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
        output_len = 0;
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
        output_len = 0;
        break;
    case HTTP_EVENT_HEADERS_SENT:
        ESP_LOGI(TAG, "HTTP_EVENT_HEADERS_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        if (evt->user_data) {
            if (output_len + evt->data_len > MAX_HTTP_OUTPUT_BUFFER) {
                ESP_LOGE(TAG, "Data does not fit the buffer!");
            } else {
                memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                output_len += evt->data_len;
            }
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
        output_len = 0;
        break;
    }
    return ESP_OK;
}

void display_test_state(pax_buf_t* pax_buffer, ILI9341* ili9341, const char* text) {
    pax_noclip(pax_buffer);
    const pax_font_t *font = pax_get_font("saira regular");
    pax_background(pax_buffer, 0xFFFFFF);
    pax_vec1_t title_size = pax_text_size(font, 18, "Testing WiFi connection...");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, (320 / 2) - (title_size.x / 2), (240 / 2) - (title_size.y / 2) - 20, "Testing WiFi connection...");
    pax_vec1_t size = pax_text_size(font, 18, text);
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, (320 / 2) - (size.x / 2), (240 / 2) - (title_size.y / 2) + 20, text);
    ili9341_write(ili9341, pax_buffer->buf);
}


void wifi_connection_test(pax_buf_t* pax_buffer, ILI9341* ili9341) {
    display_test_state(pax_buffer, ili9341, "Connecting to WiFi...");

    if (!wifi_connect_to_stored()) {
        display_test_state(pax_buffer, ili9341, "Failed to connect to WiFi");
        vTaskDelay(500 / portTICK_PERIOD_MS);
        return;
    }

    display_test_state(pax_buffer, ili9341, "Starting test...");
    esp_wifi_set_ps(WIFI_PS_NONE); // Disable any WiFi power save mode

    ESP_LOGI(TAG, "Starting connection test...");

    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};

    esp_http_client_config_t config = {
        .url = "https://hatchery.badge.team/basket/mch2021/categories/json",//"https://ota.bodge.team/test.json",
        .crt_bundle_attach = esp_crt_bundle_attach,
        .cert_pem = (char *)server_cert_pem_start,
        .event_handler = _test_http_event_handler,
        .user_data = local_response_buffer,
        .keep_alive_enable = true
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %lld",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
        display_test_state(pax_buffer, ili9341, "WiFi test completed!");
        ESP_LOG_BUFFER_HEX(TAG, local_response_buffer, strlen(local_response_buffer));
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        display_test_state(pax_buffer, ili9341, esp_err_to_name(err));
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    wifi_disconnect_and_disable();
}
