#pragma once

#include <esp_err.h>


typedef struct hatchery_server_t hatchery_server_t;
typedef struct hatchery_app_type_t hatchery_app_type_t;
typedef struct hatchery_category_t hatchery_category_t;
typedef struct hatchery_app_t hatchery_app_t;

struct hatchery_server_t {
    const char *url;
    hatchery_app_type_t *app_types;
};


struct hatchery_app_type_t {
    char *name;
    char *slug;
    hatchery_category_t *categories;
    hatchery_server_t *server;
    hatchery_app_type_t *next;
};

esp_err_t hatchery_query_app_types(hatchery_server_t *server);

void hatchery_app_type_free(hatchery_app_type_t *app_type);


struct hatchery_category_t {
    char *name;
    char *slug;
    int nr_apps; // -1: Unknown
    hatchery_app_t *apps;
    hatchery_app_type_t *app_type;
    hatchery_category_t *next;
};

esp_err_t hatchery_query_categories(hatchery_app_type_t *app_type);

void hatchery_category_free(hatchery_category_t *catagories);


typedef struct hatchery_file_t hatchery_file_t;
struct hatchery_file_t {
    char *name;
    char *url;
    int size; // -1: Unknown
    hatchery_file_t *next;
};

struct hatchery_app_t {
    char *name;
    char *slug;
    char *author;
    char *license;
    char *description;
    hatchery_file_t *files;
    hatchery_category_t *category;
    hatchery_app_t *next;
};

esp_err_t hatchery_query_apps(hatchery_category_t *category);

esp_err_t hatchery_query_app(hatchery_app_t *app);

void hatchery_app_free(hatchery_app_t *apps);
