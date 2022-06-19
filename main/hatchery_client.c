#include "hatchery_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include <esp_err.h>
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "string.h"
#include "esp_crt_bundle.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <sys/socket.h>
#include "esp_wifi.h"
#include "bootscreen.h"

#include "wifi_connect.h"

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
    if (!wifi_connect_to_stored()) {
        return ESP_ERR_ESP_NETIF_INIT_FAILED;
    }

    //esp_wifi_set_ps(WIFI_PS_NONE); // Disable any WiFi power save mode

    //ESP_LOGI(TAG, "http get");

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

    //ESP_LOGI(TAG, "http get done %d", err);

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
struct json_cb_parser_t
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

    json_cb_parser_t *parser = (json_cb_parser_t*)callback_data;

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

// Query app types

#define MAX_URL_LEN 300

typedef struct process_app_types_t process_app_types_t;
struct process_app_types_t {
    int cl;
    hatchery_server_t *server;
    hatchery_app_type_t **cur;
};

static hatchery_app_type_t *new_app_type(hatchery_server_t *server) {
    hatchery_app_type_t *app_type = (hatchery_app_type_t*)malloc(sizeof(hatchery_app_type_t ));
    if (app_type != NULL) {
        app_type->name = NULL;
        app_type->slug = NULL;
        app_type->server = server;
        app_type->categories = NULL;
        app_type->next = NULL;
    }
    return app_type;
}

static void hatchery_process_app_types(json_cb_parser_t *parser, enum json_cb_parser_state_t state)
{
    process_app_types_t *process_app_types = (process_app_types_t*)(parser->data);
#define NEXT process_app_types->cl = __LINE__; return; case __LINE__:;
    
    switch(process_app_types->cl) { case 0:
    
        if (state == json_cb_open_array) {
            NEXT
            while (state == json_cb_open_object) {
                *process_app_types->cur = new_app_type(process_app_types->server);
                NEXT
                while (state == json_cb_string) {
                    if (string_appender_compare(&parser->string_appender, "slug") == 0) {
                        NEXT
                        if (state == json_cb_string) {
                            if ((*process_app_types->cur) != NULL && (*process_app_types->cur)->slug == NULL) {
                                (*process_app_types->cur)->slug = string_appender_copy(&parser->string_appender);
                            }
                            NEXT
                        }
                    }
                    else if (string_appender_compare(&parser->string_appender, "name") == 0) {
                        NEXT
                        if (state == json_cb_string) {
                            if ((*process_app_types->cur) != NULL && (*process_app_types->cur)->name == NULL) {
                                (*process_app_types->cur)->name = string_appender_copy(&parser->string_appender);
                            }
                            NEXT
                        }
                    }
                }
                if (state == json_cb_close_object) {
                    if ((*process_app_types->cur) != NULL) {
                        process_app_types->cur = &(*process_app_types->cur)->next;
                    }
                    NEXT
                }
            }
            if (state == json_cb_close_array) {
                NEXT
            }
        }
    }

#undef NEXT
}

esp_err_t hatchery_query_app_types(hatchery_server_t *server) {

    if (server->app_types != NULL) {
        return ESP_OK;
    }

    hatchery_app_type_t *app_type = new_app_type(server);
    if (app_type != NULL) {
        app_type->name = (char*)malloc(sizeof(char)*4);
        strcpy(app_type->name, "App");
        app_type->slug = (char*)malloc(sizeof(char)*4);
        strcpy(app_type->slug, "app");
        server->app_types = app_type;
    }

    return ESP_OK;

    process_app_types_t process_app_types_data;
    process_app_types_data.cl = 0;
    process_app_types_data.server = server;
    process_app_types_data.cur = &server->app_types;
    
    json_cb_parser_t parser;
    json_cb_parser_init(&parser, hatchery_process_app_types, &process_app_types_data);

    data_callback_t data_callback;
    data_callback.data = &parser;
    data_callback.fn = json_cb_process;

    char url[MAX_URL_LEN+1];
    snprintf(url, MAX_URL_LEN, "%s/types", server->url);
    url[MAX_URL_LEN] = '\0';
    esp_err_t result = hatchery_http_get(url, &data_callback);

    json_cb_parser_close(&parser);

    return result;
}

void hatchery_app_type_free(hatchery_app_type_t *app_types) {

    while (app_types != NULL) {
        free(app_types->name);
        free(app_types->slug);
        hatchery_category_free(app_types->categories);
        hatchery_app_type_t *next = app_types->next;
        free(app_types);
        app_types = next;
    }
}


// Query categories

typedef struct process_categories_t process_categories_t;
struct process_categories_t {
    int cl;
    hatchery_app_type_t *app_type;
    hatchery_category_t **cur;
};

static hatchery_category_t *new_category(hatchery_app_type_t *app_type) {
    hatchery_category_t *category = (hatchery_category_t*)malloc(sizeof(hatchery_category_t ));
    if (category != NULL) {
        category->name = NULL;
        category->slug = NULL;
        category->nr_apps = -1; // Unknown
        category->app_type = app_type;
        category->apps = NULL;
        category->next = NULL;
    }
    return category;
}

static void hatchery_process_categories(json_cb_parser_t *parser, enum json_cb_parser_state_t state)
{
    process_categories_t *process_categories = (process_categories_t*)(parser->data);
#define NEXT process_categories->cl = __LINE__; return; case __LINE__:;
    
    switch(process_categories->cl) { case 0:
    
        if (state == json_cb_open_array) {
            NEXT
            while (state == json_cb_open_object) {
                *process_categories->cur = new_category(process_categories->app_type);
                NEXT
                while (state == json_cb_string) {
                    if (string_appender_compare(&parser->string_appender, "slug") == 0) {
                        NEXT
                        if (state == json_cb_string) {
                            if ((*process_categories->cur) != NULL && (*process_categories->cur)->slug == NULL) {
                                (*process_categories->cur)->slug = string_appender_copy(&parser->string_appender);
                            }
                            NEXT
                        }
                    }
                    else if (string_appender_compare(&parser->string_appender, "name") == 0) {
                        NEXT
                        if (state == json_cb_string) {
                            if ((*process_categories->cur) != NULL && (*process_categories->cur)->name == NULL) {
                                (*process_categories->cur)->name = string_appender_copy(&parser->string_appender);
                            }
                            NEXT
                        }
                    }
                    else if (string_appender_compare(&parser->string_appender, "apps") == 0) {
                        NEXT
                        if (state == json_cb_int) {
                            if ((*process_categories->cur) != NULL && (*process_categories->cur)->nr_apps == -1) {
                                (*process_categories->cur)->nr_apps = parser->int_value;
                            }
                        }
                    }
                }
                if (state == json_cb_close_object) {
                    if ((*process_categories->cur) != NULL) {
                        process_categories->cur = &(*process_categories->cur)->next;
                    }
                    NEXT
                }
            }
            if (state == json_cb_close_array) {
                NEXT
            }
        }
    }

#undef NEXT
}

esp_err_t hatchery_query_categories(hatchery_app_type_t *app_type) {

    if (app_type->categories != NULL) {
        return ESP_OK;
    }

    process_categories_t process_categories_data;
    process_categories_data.cl = 0;
    process_categories_data.app_type = app_type;
    process_categories_data.cur = &app_type->categories;
    
    json_cb_parser_t parser;
    json_cb_parser_init(&parser, hatchery_process_categories, &process_categories_data);

    data_callback_t data_callback;
    data_callback.data = &parser;
    data_callback.fn = json_cb_process;

    char url[MAX_URL_LEN+1];
    snprintf(url, MAX_URL_LEN, "%s/%s/categories", app_type->server->url, app_type->slug);
    url[MAX_URL_LEN] = '\0';
    const char *urltest = app_type->server->url;
    esp_err_t result = hatchery_http_get(urltest, &data_callback);

    json_cb_parser_close(&parser);

    return result;
}

void hatchery_category_free(hatchery_category_t *catagories) {

    while (catagories != NULL) {
        free(catagories->slug);
        free(catagories->name);
        hatchery_app_free(catagories->apps);
        hatchery_category_t *next = catagories->next;
        free(catagories);
        catagories = next;
    }
}

// Query apps

typedef struct process_apps_t process_apps_t;
struct process_apps_t {
    int cl;
    hatchery_category_t *category;
    hatchery_app_t **cur;
};

static hatchery_app_t *new_app(hatchery_category_t *category) {
    hatchery_app_t *app = (hatchery_app_t*)malloc(sizeof(hatchery_app_t ));
    if (app != NULL) {
        app->name = NULL;
        app->slug = NULL; 
        app->author = NULL;
        app->license = NULL;
        app->description = NULL;
        app->files = NULL;
        app->category = category;
        app->next = NULL;
    }
    return app;
}

static void hatchery_process_apps(json_cb_parser_t *parser, enum json_cb_parser_state_t state)
{
    process_apps_t *process_apps = (process_apps_t*)(parser->data);
#define NEXT process_apps->cl = __LINE__; return; case __LINE__:;
    
    switch(process_apps->cl) { case 0:
    
        if (state == json_cb_open_array) {
            NEXT
            while (state == json_cb_open_object) {
                *process_apps->cur = new_app(process_apps->category);
                NEXT
                while (state == json_cb_string) {
                    if (string_appender_compare(&parser->string_appender, "slug") == 0) {
                        NEXT
                        if (state == json_cb_string) {
                            if ((*process_apps->cur) != NULL && (*process_apps->cur)->slug == NULL) {
                                (*process_apps->cur)->slug = string_appender_copy(&parser->string_appender);
                            }
                            NEXT
                        }
                    }
                    else if (string_appender_compare(&parser->string_appender, "name") == 0) {
                        NEXT
                        if (state == json_cb_string) {
                            if ((*process_apps->cur) != NULL && (*process_apps->cur)->name == NULL) {
                                (*process_apps->cur)->name = string_appender_copy(&parser->string_appender);
                            }
                            NEXT
                        }
                    }
                    else if (string_appender_compare(&parser->string_appender, "author") == 0) {
                        NEXT
                        if (state == json_cb_string) {
                            if ((*process_apps->cur) != NULL && (*process_apps->cur)->author == NULL) {
                                (*process_apps->cur)->author = string_appender_copy(&parser->string_appender);
                            }
                            NEXT
                        }
                    }
                    else if (string_appender_compare(&parser->string_appender, "license") == 0) {
                        NEXT
                        if (state == json_cb_string) {
                            if ((*process_apps->cur) != NULL && (*process_apps->cur)->license == NULL) {
                                (*process_apps->cur)->license = string_appender_copy(&parser->string_appender);
                            }
                            NEXT
                        }
                    }
                    else if (string_appender_compare(&parser->string_appender, "description") == 0) {
                        NEXT
                        if (state == json_cb_string) {
                            if ((*process_apps->cur) != NULL && (*process_apps->cur)->description == NULL) {
                                (*process_apps->cur)->description = string_appender_copy(&parser->string_appender);
                            }
                            NEXT
                        }
                    }
                }
                if (state == json_cb_close_object) {
                    if ((*process_apps->cur) != NULL) {
                        process_apps->cur = &(*process_apps->cur)->next;
                    }
                    NEXT
                }
            }
            if (state == json_cb_close_array) {
                NEXT
            }
        }
    }

#undef NEXT
}

esp_err_t hatchery_query_apps(hatchery_category_t *category) {

    if (category->apps != NULL) {
        return ESP_OK;
    }

    process_apps_t process_apps_data;
    process_apps_data.cl = 0;
    process_apps_data.category = category;
    process_apps_data.cur = &category->apps;
    
    json_cb_parser_t parser;
    json_cb_parser_init(&parser, hatchery_process_apps, &process_apps_data);

    data_callback_t data_callback;
    data_callback.data = &parser;
    data_callback.fn = json_cb_process;

    hatchery_app_type_t *app_type = category->app_type;
    char url[MAX_URL_LEN+1];
    snprintf(url, MAX_URL_LEN, "%s/%s/%s/apps", app_type->server->url, app_type->slug, category->slug);
    url[MAX_URL_LEN] = '\0';
    esp_err_t result = hatchery_http_get(url, &data_callback);

    json_cb_parser_close(&parser);

    return result;
}

void hatchery_app_free(hatchery_app_t *apps) {

    while (apps != NULL) {
        free(apps->slug);
        free(apps->name);
        free(apps->author);
        free(apps->license);
        free(apps->description);
        for (hatchery_file_t *files = apps->files; files != NULL;) {
            free(files->name);
            free(files->url);
            hatchery_file_t *next_file = files->next;
            free(files);
            files = next_file;
        }
        hatchery_app_t *next = apps->next;
        free(apps);
        apps = next;
    }
}

// Query app

static hatchery_file_t *new_file() {
    hatchery_file_t *file = (hatchery_file_t*)malloc(sizeof(hatchery_file_t));
    if (file != NULL) {
        file->name = NULL;
        file->url = NULL;
        file->size = -1;
        file->next = NULL;
    }
    return file;
}

typedef struct process_app_t process_app_t;
struct process_app_t {
    int cl;
    hatchery_app_t *app;
    hatchery_file_t **cur;
};

static void replace_string(char **ref_trg, string_appender_t *string_appender) {
    if (*ref_trg == NULL || string_appender_compare(string_appender, *ref_trg) != 0) {
        free(*ref_trg);
        *ref_trg = string_appender_copy(string_appender);
    }
}

static void hatchery_process_app(json_cb_parser_t *parser, enum json_cb_parser_state_t state)
{
    process_app_t *process_app = (process_app_t*)(parser->data);
#define NEXT process_app->cl = __LINE__; return; case __LINE__:;
    
    switch(process_app->cl) { case 0:
    
        if (state == json_cb_open_object) {
            NEXT
            while (state == json_cb_string) {
                if (string_appender_compare(&parser->string_appender, "slug") == 0) {
                    NEXT
                    if (state == json_cb_string) {
                        replace_string(&process_app->app->slug, &parser->string_appender);
                        NEXT
                    }
                }
                else if (string_appender_compare(&parser->string_appender, "name") == 0) {
                    NEXT
                    if (state == json_cb_string) {
                        replace_string(&process_app->app->name, &parser->string_appender);
                        NEXT
                    }
                }
                else if (string_appender_compare(&parser->string_appender, "author") == 0) {
                    NEXT
                    if (state == json_cb_string) {
                        replace_string(&process_app->app->author, &parser->string_appender);
                        NEXT
                    }
                }
                else if (string_appender_compare(&parser->string_appender, "license") == 0) {
                    NEXT
                    if (state == json_cb_string) {
                        replace_string(&process_app->app->license, &parser->string_appender);
                        NEXT
                    }
                }
                else if (string_appender_compare(&parser->string_appender, "description") == 0) {
                    NEXT
                    if (state == json_cb_string) {
                        replace_string(&process_app->app->description, &parser->string_appender);
                        NEXT
                    }
                }
                else if (string_appender_compare(&parser->string_appender, "files") == 0) {
                    NEXT
                    if (state == json_cb_open_array) {
                        NEXT
                        while (state == json_cb_open_object) {
                            (*process_app->cur) = new_file();
                            NEXT
                            while (state == json_cb_string) {
                                if (string_appender_compare(&parser->string_appender, "name") == 0) {
                                    NEXT
                                    if (state == json_cb_string) {
                                        if ((*process_app->cur) != NULL && (*process_app->cur)->name == NULL) {
                                            (*process_app->cur)->name = string_appender_copy(&parser->string_appender);
                                        }
                                        NEXT
                                    }
                                }
                                else if (string_appender_compare(&parser->string_appender, "url") == 0) {
                                    NEXT
                                    if (state == json_cb_string) {
                                        if ((*process_app->cur) != NULL && (*process_app->cur)->url == NULL) {
                                            (*process_app->cur)->url = string_appender_copy(&parser->string_appender);
                                        }
                                        NEXT
                                    }
                                }
                                else if (string_appender_compare(&parser->string_appender, "size") == 0) {
                                    NEXT
                                    if (state == json_cb_int) {
                                        if ((*process_app->cur) != NULL && (*process_app->cur)->size == -1) {
                                            (*process_app->cur)->size = parser->int_value;
                                        }
                                    }
                                }
                            }
                            if (state == json_cb_close_object) {
                                if ((*process_app->cur) != NULL) {
                                    process_app->cur = &(*process_app->cur)->next;
                                }
                                NEXT
                            }
                        }
                        if (state == json_cb_close_array) {
                            NEXT
                        }
                    }
                }
            }
            if (state == json_cb_open_object) {
                NEXT
            }
        }
    }

#undef NEXT
}

esp_err_t hatchery_query_app(hatchery_app_t *app) {

    if (app->files != NULL) {
        return ESP_OK;
    }

    process_app_t process_app_data;
    process_app_data.cl = 0;
    process_app_data.app = app;
    process_app_data.cur = &app->files;
    
    json_cb_parser_t parser;
    json_cb_parser_init(&parser, hatchery_process_app, &process_app_data);

    data_callback_t data_callback;
    data_callback.data = &parser;
    data_callback.fn = json_cb_process;

    hatchery_category_t *category = app->category;
    hatchery_app_type_t *app_type = category->app_type;
    char url[MAX_URL_LEN+1];
    snprintf(url, MAX_URL_LEN, "%s/%s/%s/%s", app_type->server->url, app_type->slug, category->slug, app->slug);
    url[MAX_URL_LEN] = '\0';
    esp_err_t result = hatchery_http_get(url, &data_callback);

    json_cb_parser_close(&parser);

    return result;
}
