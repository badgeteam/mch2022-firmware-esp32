#include "menu.h"

#include <stdio.h>
#include <string.h>

#include "pax_codecs.h"
#include "pax_gfx.h"

menu_t* menu_alloc(const char* title, float entry_height, float text_height) {
    if (title == NULL) return NULL;
    menu_t* menu = malloc(sizeof(menu_t));
    if (menu == NULL) return NULL;
    size_t titleSize = strlen(title) + 1;
    menu->title      = malloc(titleSize);
    if (menu->title == NULL) {
        free(menu);
        return NULL;
    }
    memcpy(menu->title, title, titleSize);
    menu->firstItem    = NULL;
    menu->length       = 0;
    menu->position     = 0;
    menu->entry_height = (entry_height > 0) ? entry_height : 20;
    menu->text_height  = (text_height > 0) ? text_height : (entry_height - 2);
    menu->icon         = NULL;

    menu->fgColor           = 0xFF000000;
    menu->bgColor           = 0xFFFFFFFF;
    menu->bgTextColor       = 0xFFFFFFFF;
    menu->selectedItemColor = 0xFF000000;
    menu->borderColor       = 0x88000000;
    menu->titleColor        = 0xFFFFFFFF;
    menu->titleBgColor      = 0xFF000000;
    menu->scrollbarBgColor  = 0xFFCCCCCC;
    menu->scrollbarFgColor  = 0xFF555555;

    return menu;
}

void _menu_free_item(menu_item_t* menu_item) {
    free(menu_item->label);
    free(menu_item);
}

void menu_free(menu_t* menu) {
    if (menu == NULL) return;
    free(menu->title);
    menu_item_t* currentItem = menu->firstItem;
    while (currentItem != NULL) {
        menu_item_t* nextItem = currentItem->nextItem;
        _menu_free_item(currentItem);
        currentItem = nextItem;
    }
    free(menu);
}

void menu_set_icon(menu_t* menu, pax_buf_t* icon) { menu->icon = icon; }

menu_item_t* _menu_find_item(menu_t* menu, size_t position) {
    menu_item_t* currentItem = menu->firstItem;
    if (currentItem == NULL) return NULL;
    size_t index = 0;
    while (index < position) {
        if (currentItem->nextItem == NULL) break;
        currentItem = currentItem->nextItem;
        index++;
    }
    return currentItem;
}

menu_item_t* _menu_find_last_item(menu_t* menu) {
    menu_item_t* lastItem = menu->firstItem;
    if (lastItem == NULL) return NULL;
    while (lastItem->nextItem != NULL) {
        lastItem = lastItem->nextItem;
    }
    return lastItem;
}

bool menu_insert_item(menu_t* menu, const char* aLabel, menu_callback_t callback, void* callback_arguments, size_t position) {
    if (menu == NULL) return false;
    menu_item_t* newItem = malloc(sizeof(menu_item_t));
    if (newItem == NULL) return false;
    size_t labelSize = strlen(aLabel) + 1;
    newItem->label   = malloc(labelSize);
    if (newItem->label == NULL) {
        free(newItem);
        return false;
    }
    memcpy(newItem->label, aLabel, labelSize);
    newItem->callback           = callback;
    newItem->callback_arguments = callback_arguments;
    newItem->icon               = NULL;
    if (menu->firstItem == NULL) {
        newItem->nextItem     = NULL;
        newItem->previousItem = NULL;
        menu->firstItem       = newItem;
    } else {
        if (position >= menu->length) {
            newItem->previousItem           = _menu_find_last_item(menu);
            newItem->nextItem               = NULL;
            newItem->previousItem->nextItem = newItem;
        } else {
            newItem->nextItem     = _menu_find_item(menu, position);
            newItem->previousItem = newItem->nextItem->previousItem;                       // Copy pointer to previous item to new item
            if (newItem->nextItem != NULL) newItem->nextItem->previousItem = newItem;      // Replace pointer to previous item with new item
            if (newItem->previousItem != NULL) newItem->previousItem->nextItem = newItem;  // Replace pointer to next item in previous item
        }
    }
    menu->length++;
    return true;
}

bool menu_insert_item_icon(menu_t* menu, const char* aLabel, menu_callback_t callback, void* callback_arguments, size_t position, pax_buf_t* icon) {
    if (!menu_insert_item(menu, aLabel, callback, callback_arguments, position)) {
        return false;
    }
    menu_item_t* item;
    if (position >= menu->length - 1) {
        item = _menu_find_last_item(menu);
    } else {
        item = _menu_find_item(menu, position);
    }

    item->icon = icon;
    return true;
}

bool menu_remove_item(menu_t* menu, size_t position) {
    if (menu == NULL) return false;              // Can't delete an item from a menu that doesn't exist
    if (menu->length <= position) return false;  // Can't delete an item that doesn't exist
    menu_item_t* item;

    if (position == 0) {
        item = menu->firstItem;
        if (item == NULL) return false;  // Can't delete if no linked list is allocated
        if (item->nextItem != NULL) {
            menu->firstItem               = item->nextItem;
            menu->firstItem->previousItem = NULL;
        } else {
            menu->firstItem = NULL;
        }
    } else {
        item = _menu_find_item(menu, position);
        if (item == NULL) return false;
        if (item->previousItem != NULL) item->previousItem->nextItem = item->nextItem;
        if (item->nextItem != NULL) item->nextItem->previousItem = item->previousItem;
    }
    free(item->label);
    free(item);
    menu->length--;
    printf("%u / %u\n", menu->position, menu->length);
    if (menu->position >= menu->length) {
        menu->position = menu->length - 1;
        printf("Corrected to %u / %u\n", menu->position, menu->length);
    }
    return true;
}

bool menu_navigate_to(menu_t* menu, size_t position) {
    if (menu == NULL) return false;
    if (menu->length < 1) return false;
    menu->position = position;
    if (menu->position >= menu->length) menu->position = menu->length - 1;
    return true;
}

void menu_navigate_previous(menu_t* menu) {
    if (menu == NULL) return;
    if (menu->length < 1) return;
    menu->position--;
    if (menu->position > menu->length) {
        menu->position = menu->length - 1;
    }
}

void menu_navigate_next(menu_t* menu) {
    if (menu == NULL) return;
    if (menu->length < 1) return;
    menu->position = (menu->position + 1) % menu->length;
}

size_t menu_get_position(menu_t* menu) { return menu->position; }

size_t menu_get_length(menu_t* menu) { return menu->length; }

void* menu_get_callback_args(menu_t* menu, size_t position) {
    menu_item_t* item = _menu_find_item(menu, position);
    if (item == NULL) return NULL;
    return item->callback_arguments;
}

pax_buf_t* menu_get_icon(menu_t* menu, size_t position) {
    menu_item_t* item = _menu_find_item(menu, position);
    if (item == NULL) return NULL;
    return item->icon;
}

void menu_debug(menu_t* menu) {
    if (menu == NULL) {
        printf("Menu pointer is NULL\n");
        return;
    }
    printf("Title:    %s\n", menu->title);
    printf("Length:   %u\n", menu->length);
    printf("Position: %u\n", menu->position);
    menu_item_t* item = menu->firstItem;
    if (item == NULL) {
        printf("Menu contains no items\n");
    } else {
        while (item != NULL) {
            printf("> %s\n", item->label);
            item = item->nextItem;
        }
    }
    printf("------\n");
}

void menu_render(pax_buf_t* buffer, menu_t* menu, float position_x, float position_y, float width, float height, pax_col_t color) {
    const pax_font_t* font = pax_get_font("saira regular");

    float  entry_height = menu->entry_height;  // 18 + 2;
    float  text_height  = menu->text_height;
    float  text_offset  = ((entry_height - text_height) / 2) + 1;
    size_t maxItems     = height / entry_height;

    float current_position_y = position_y;

    pax_noclip(buffer);

    if (maxItems > 1) {
        float offsetX = 0;
        if (menu->icon != NULL) {
            offsetX = 32;  // Fixed width by choice, could also use "menu->icon->width"
        }

        maxItems--;
        pax_simple_rect(buffer, menu->titleBgColor, position_x, current_position_y, width, entry_height);
        // pax_simple_line(buffer, menu->titleColor, position_x + 1, position_y + entry_height, position_x + width - 2, position_y + entry_height - 1);
        pax_clip(buffer, position_x + 1, current_position_y + text_offset, width - 2, text_height);
        pax_draw_text(buffer, menu->titleColor, font, text_height, position_x + offsetX + 1, current_position_y + text_offset, menu->title);
        if (menu->icon != NULL) {
            pax_clip(buffer, position_x, current_position_y, 32, 32);
            pax_draw_image(buffer, menu->icon, position_x, current_position_y);
        }
        pax_noclip(buffer);
        current_position_y += entry_height;
    }

    size_t itemOffset = 0;
    if (menu->position >= maxItems) {
        itemOffset = menu->position - maxItems + 1;
    }

    pax_outline_rect(buffer, menu->borderColor, position_x, position_y, width, height);
    pax_simple_rect(buffer, menu->bgColor, position_x, current_position_y, width, height - current_position_y + position_y);

    for (size_t index = itemOffset; (index < itemOffset + maxItems) && (index < menu->length); index++) {
        menu_item_t* item = _menu_find_item(menu, index);
        if (item == NULL) {
            printf("Render error: item is NULL at %u\n", index);
            break;
        }

        float iconWidth = 0;
        if (item->icon != NULL) {
            iconWidth = 33;  // Fixed width by choice, could also use "item->icon->width + 1"
        }

        if (index == menu->position) {
            pax_simple_rect(buffer, menu->selectedItemColor, position_x + 1, current_position_y, width - 2, entry_height);
            pax_clip(buffer, position_x + 1, current_position_y + text_offset, width - 4, text_height);
            pax_draw_text(buffer, menu->bgTextColor, font, text_height, position_x + iconWidth + 1, current_position_y + text_offset, item->label);
            pax_noclip(buffer);
        } else {
            pax_simple_rect(buffer, menu->bgColor, position_x + 1, current_position_y, width - 2, entry_height);
            pax_clip(buffer, position_x + 1, current_position_y + text_offset, width - 4, text_height);
            pax_draw_text(buffer, menu->fgColor, font, text_height, position_x + iconWidth + 1, current_position_y + text_offset, item->label);
            pax_noclip(buffer);
        }

        if (item->icon != NULL) {
            pax_draw_image(buffer, item->icon, position_x + 1, current_position_y);
        }

        current_position_y += entry_height;
    }

    pax_clip(buffer, position_x + width - 5, position_y + entry_height, 4, height - 1 - entry_height);

    float fractionStart = itemOffset / (menu->length * 1.0);
    float fractionEnd   = (itemOffset + maxItems) / (menu->length * 1.0);
    if (fractionEnd > 1.0) fractionEnd = 1.0;

    float scrollbarHeight = height - entry_height;
    float scrollbarStart  = scrollbarHeight * fractionStart;
    float scrollbarEnd    = scrollbarHeight * fractionEnd;

    pax_simple_rect(buffer, menu->scrollbarBgColor, position_x + width - 5, position_y + entry_height - 1, 4, scrollbarHeight);
    pax_simple_rect(buffer, menu->scrollbarFgColor, position_x + width - 5, position_y + entry_height - 1 + scrollbarStart, 4, scrollbarEnd - scrollbarStart);

    pax_noclip(buffer);
}
