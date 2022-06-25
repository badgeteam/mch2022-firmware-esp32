#pragma once

#ifdef __cplusplus
extern "C" {
#endif  //__cplusplus

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pax_gfx.h"

typedef bool (*menu_callback_t)();

typedef struct _menu_item {
    char*           label;
    menu_callback_t callback;
    void*           callbackArgs;

    pax_buf_t* icon;

    // Linked list
    struct _menu_item* previousItem;
    struct _menu_item* nextItem;
} menu_item_t;

typedef struct menu {
    char*        title;
    menu_item_t* firstItem;
    size_t       length;
    size_t       position;
    float        entry_height;
    float        text_height;
    pax_buf_t*   icon;

    pax_col_t fgColor;
    pax_col_t bgColor;
    pax_col_t selectedItemColor;
    pax_col_t bgTextColor;
    pax_col_t borderColor;
    pax_col_t titleColor;
    pax_col_t titleBgColor;
    pax_col_t scrollbarBgColor;
    pax_col_t scrollbarFgColor;

} menu_t;

menu_t*    menu_alloc(const char* aTitle, float arg_entry_height, float arg_text_height);
void       menu_free(menu_t* aMenu);
void       menu_set_icon(menu_t* aMenu, pax_buf_t* icon);
bool       menu_insert_item(menu_t* aMenu, const char* aLabel, menu_callback_t aCallback, void* aCallbackArgs, size_t aPosition);
bool       menu_insert_item_icon(menu_t* aMenu, const char* aLabel, menu_callback_t aCallback, void* aCallbackArgs, size_t aPosition, pax_buf_t* icon);
bool       menu_remove_item(menu_t* aMenu, size_t aPosition);
bool       menu_navigate_to(menu_t* aMenu, size_t aPosition);
void       menu_navigate_previous(menu_t* aMenu);
void       menu_navigate_next(menu_t* aMenu);
size_t     menu_get_position(menu_t* aMenu);
size_t     menu_get_length(menu_t* aMenu);
void*      menu_get_callback_args(menu_t* aMenu, size_t aPosition);
pax_buf_t* menu_get_icon(menu_t* aMenu, size_t aPosition);
void       menu_debug(menu_t* aMenu);
void       menu_render(pax_buf_t* aBuffer, menu_t* aMenu, float aPosX, float aPosY, float aWidth, float aHeight, pax_col_t aColor);

#ifdef __cplusplus
}
#endif  //__cplusplus
