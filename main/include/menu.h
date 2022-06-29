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
    void*           callback_arguments;

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

    float grid_margin_x;
    float grid_margin_y;
    float grid_entry_count_x;
    float grid_entry_count_y;
} menu_t;

menu_t*    menu_alloc(const char* title, float entry_height, float text_height);
void       menu_free(menu_t* menu);
void       menu_set_icon(menu_t* menu, pax_buf_t* icon);
bool       menu_insert_item(menu_t* menu, const char* label, menu_callback_t callback, void* callback_arguments, size_t position);
bool       menu_insert_item_icon(menu_t* menu, const char* label, menu_callback_t callback, void* callback_arguments, size_t position, pax_buf_t* icon);
bool       menu_remove_item(menu_t* menu, size_t position);
bool       menu_navigate_to(menu_t* menu, size_t position);
void       menu_navigate_previous(menu_t* menu);
void       menu_navigate_next(menu_t* menu);
void       menu_navigate_previous_row(menu_t* menu);
void       menu_navigate_next_row(menu_t* menu);
size_t     menu_get_position(menu_t* menu);
void       menu_set_position(menu_t* menu, size_t position);
size_t     menu_get_length(menu_t* menu);
void*      menu_get_callback_args(menu_t* menu, size_t position);
pax_buf_t* menu_get_icon(menu_t* menu, size_t position);
void       menu_debug(menu_t* menu);
void       menu_render(pax_buf_t* pax_buffer, menu_t* menu, float position_x, float position_y, float width, float height);
void       menu_render_grid(pax_buf_t* buffer, menu_t* menu, float position_x, float position_y, float width, float height);

#ifdef __cplusplus
}
#endif  //__cplusplus
