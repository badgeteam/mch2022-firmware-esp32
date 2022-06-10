#pragma once

#include <esp_err.h>


typedef struct hatchery_category_t hatchery_category_t;
struct hatchery_category_t {
    char *name;
    char *slug;
    int nr_eggs;
    hatchery_category_t *next;
};

esp_err_t hatchery_query_categories(const char *url, hatchery_category_t **ref_categories);

void hatchery_category_free(hatchery_category_t *catagories);
