#include "hatchery.h"

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

#define HASH_LEN 32

static const char *TAG = "Hatchery";

extern const uint8_t server_cert_pem_start[] asm("_binary_isrgrootx1_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_isrgrootx1_pem_end");

typedef struct data_callback_t data_callback_t;
struct data_callback_t {
    void *data;
    void (*fn)(void *callback_data, const char *data, int data_len);
};

static esp_err_t _http_event_handler_data_callback(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADERS_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADERS_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        data_callback_t *data_callback = (data_callback_t*)evt->user_data;
        data_callback->fn(data_callback->data, (const char*)evt->data, evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    }
    return ESP_OK;
}

static esp_err_t hatchery_http_get(const char *url, data_callback_t *data_callback)
{
    esp_wifi_set_ps(WIFI_PS_NONE); // Disable any WiFi power save mode

    ESP_LOGI(TAG, "http get");

    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .cert_pem = (char *)server_cert_pem_start,
        .event_handler = _http_event_handler_data_callback,
        .user_data = data_callback,
        .keep_alive_enable = true
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "http get done %d", err);

    return err;
}

// String appender

typedef struct string_appender_t string_appender_t;

typedef struct string_buffer_t string_buffer_t;
struct string_buffer_t {
	char buffer[100];
	string_buffer_t* next;
}; 

struct string_appender_t {
	string_buffer_t *head;
	string_buffer_t **cur;
	int tot_len;
	int cur_len;
};

static void string_appender_init(string_appender_t *str_app)
{
	str_app->head = 0;
	str_app->cur = &str_app->head;
	str_app->tot_len = 0;
	str_app->cur_len = 0;
}

static void string_appender_clear(string_appender_t *str_app)
{
	str_app->tot_len = 0;
	str_app->cur_len = 0;
}

static void string_appender_close(string_appender_t *str_app)
{
	string_buffer_t *buf = str_app->head;
	while (buf != 0)
	{
		string_buffer_t *cur = buf;
		buf = buf->next;
		free(cur);
	}
}

static void string_appender_append(string_appender_t *str_app, char ch)
{
	if (*str_app->cur == 0) {
		*str_app->cur = (string_buffer_t*)malloc(sizeof(string_buffer_t));
		if (str_app->cur == 0) {
			return;
		}
		(*str_app->cur)->next = 0;
	}
	(*str_app->cur)->buffer[str_app->cur_len++] = ch;
	if (str_app->cur_len == 100)
	{
		str_app->cur = &(*str_app->cur)->next;
		str_app->cur_len = 0;
	}
	str_app->tot_len++;
}

static char *string_appender_copy(string_appender_t *str_app)
{
	char *result = (char*)malloc((str_app->tot_len + 1)*sizeof(char));
	if (result == 0) {
		return 0;
	}
	int tot_len = str_app->tot_len;
	int cur_pos = 0;
	string_buffer_t *buf = str_app->head;
	for (int i = 0; i < tot_len; i++) {
		result[i] = buf->buffer[cur_pos++];
		if (cur_pos == 100) {
			buf = buf->next;
			cur_pos = 0;
		}
	}
	result[tot_len] = '\0';
	return result;
}

static int string_appender_compare(string_appender_t *str_app, const char *s)
{
	int tot_len = str_app->tot_len;
	int cur_pos = 0;
	string_buffer_t *buf = str_app->head;
	for (int i = 0; i < tot_len; i++) {
		int c = *s++ - buf->buffer[cur_pos++];
		if (c != 0) {
			return c;
		}
		if (cur_pos == 100) {
			buf = buf->next;
			cur_pos = 0;
		}
	}
	return 0;
}

// JSON callback parser

typedef enum json_cb_parser_state_t json_parser_state_t;
enum json_cb_parser_state_t {
	json_cb_int,
	json_cb_string,
	json_cb_open_object,
	json_cb_close_object,
	json_cb_open_array,
	json_cb_close_array,
};

typedef struct json_cb_parser_t json_cb_parser_t;
typedef void (*json_cb_parser_callback_t)(json_cb_parser_t *parser, json_parser_state_t state);
static struct json_cb_parser_t
{
	int lc;
	string_appender_t string_appender;
	int int_value;
	json_cb_parser_callback_t callback;
	void *data;
};

static void json_cb_parser_init(json_cb_parser_t *parser, json_cb_parser_callback_t callback, void *data)
{
	parser->lc = 0;
	string_appender_init(&parser->string_appender);
	parser->callback = callback;
	parser->data = data;
}

static void json_cb_parser_close(json_cb_parser_t *parser)
{
	string_appender_close(&parser->string_appender);
}

static void json_cb_process(void *callback_data, const char *data, int data_len)
{
#define JSON_NEXT_CH parser->lc = __LINE__; goto next; case __LINE__:;

    json_cb_parser_t *parser = (json_cb_parser_t*)data;

    for (int i = 0; i < data_len; i++) {
        char ch = data[i];

        switch(parser->lc) { case 0:

            for (;;) {
                if ('0' <= ch && ch <= '9') {
                    parser->int_value = 0;
                    do {
                        parser->int_value = 10 * parser->int_value + ch - '0';
                        JSON_NEXT_CH
                    } while ('0' <= ch && ch <= '9');
                    parser->callback(parser, json_cb_int);
                }
                if (ch == '"') {
                    string_appender_clear(&parser->string_appender);
                    JSON_NEXT_CH
                    while (ch != '"') {
                        if (ch == '\\') {
                            JSON_NEXT_CH
                            if (ch == 't')
                                ch = '\t';
                            else if (ch == 'n')
                                ch = '\n';
                            else if (ch == 'r')
                                ch = '\r';
                            string_appender_append(&parser->string_appender, ch);
                        }
                        else {
                            string_appender_append(&parser->string_appender, ch);
                        }
                        JSON_NEXT_CH
                    }
                    parser->callback(parser, json_cb_string);				
                }
                else if (ch == '{') {
                    parser->callback(parser, json_cb_open_object);
                }
                else if (ch == '}') {
                    parser->callback(parser, json_cb_close_object);
                }
                else if (ch == '[') {
                    parser->callback(parser, json_cb_open_array);
                }
                else if (ch == ']') {
                    parser->callback(parser, json_cb_close_array);
                }
                else {
                    // skip
                }
                JSON_NEXT_CH
            }
        }

        next:;
    }       
 #undef JSON_NEXT_CH
}