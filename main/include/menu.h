#pragma once

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "pax_gfx.h"

typedef bool (*menu_callback_t)();

typedef struct _menu_item {
    char* label;
    menu_callback_t callback;
    void* callbackArgs;

    // Linked list
    struct _menu_item* previousItem;
    struct _menu_item* nextItem;
} menu_item_t;

typedef struct menu {
    char* title;
    menu_item_t* firstItem;
    size_t length;
    size_t position;
} menu_t;

menu_t* menu_alloc(const char* aTitle);
void menu_free(menu_t* aMenu);
bool menu_insert_item(menu_t* aMenu, const char* aLabel, menu_callback_t aCallback, void* aCallbackArgs, size_t aPosition);
bool menu_remove_item(menu_t* aMenu, size_t aPosition);
bool menu_navigate_to(menu_t* aMenu, size_t aPosition);
void menu_navigate_previous(menu_t* aMenu);
void menu_navigate_next(menu_t* aMenu);
size_t menu_get_position(menu_t* aMenu);
size_t menu_get_length(menu_t* aMenu);
void* menu_get_callback_args(menu_t* aMenu, size_t aPosition);
void menu_debug(menu_t* aMenu);
void menu_render(pax_buf_t *aBuffer, menu_t *aMenu, float aPosX, float aPosY, float aWidth, float aHeight, pax_col_t aColor);

#ifdef __cplusplus
}
#endif //__cplusplus
