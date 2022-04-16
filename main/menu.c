#include <stdio.h>
#include <string.h>
#include "pax_gfx.h"
#include "menu.h"

menu_t* menu_alloc(const char* aTitle) {
    if (aTitle == NULL) return NULL;
    menu_t* menu = malloc(sizeof(menu_t));
    if (menu == NULL) return NULL;
    size_t titleSize = strlen(aTitle) + 1;
    menu->title = malloc(titleSize);
    if (menu->title == NULL) {
        free(menu);
        return NULL;
    }
    memcpy(menu->title, aTitle, titleSize);
    menu->firstItem = NULL;
    menu->length = 0;
    menu->position = 0;
    return menu;
}

void _menu_free_item(menu_item_t* aMenuItem) {
    free(aMenuItem->label);
    //if (aMenuItem->callbackArgs != NULL) {
    //    free(aMenuItem->callbackArgs);
    //}
    free(aMenuItem);
}

void menu_free(menu_t* aMenu) {
    if (aMenu == NULL) return;
    free(aMenu->title);
    menu_item_t* currentItem = aMenu->firstItem;
    while (currentItem != NULL) {
        menu_item_t* nextItem = currentItem->nextItem;
        _menu_free_item(currentItem);
        currentItem = nextItem;
    }
    free(aMenu);
}

menu_item_t* _menu_find_item(menu_t* aMenu, size_t aPosition) {
    menu_item_t* currentItem = aMenu->firstItem;
    if (currentItem == NULL) return NULL;
    size_t index = 0;
    while (index < aPosition) {
        if (currentItem->nextItem == NULL) break;
        currentItem = currentItem->nextItem;
        index++;
    }
    return currentItem;
}

menu_item_t* _menu_find_last_item(menu_t* aMenu) {
    menu_item_t* lastItem = aMenu->firstItem;
    if (lastItem == NULL) return NULL;
    while (lastItem->nextItem != NULL) {
        lastItem = lastItem->nextItem;
    }
    return lastItem;
}

bool menu_insert_item(menu_t* aMenu, const char* aLabel, menu_callback_t aCallback, void* aCallbackArgs, size_t aPosition) {
    if (aMenu == NULL) return false;
    menu_item_t* newItem = malloc(sizeof(menu_item_t));
    if (newItem == NULL) return false;
    size_t labelSize = strlen(aLabel) + 1;
    newItem->label = malloc(labelSize);
    if (newItem->label == NULL) {
        free(newItem);
        return NULL;
    }
    memcpy(newItem->label, aLabel, labelSize);
    newItem->callback = aCallback;
    newItem->callbackArgs = aCallbackArgs;
    if (aMenu->firstItem == NULL) {
        newItem->nextItem = NULL;
        newItem->previousItem = NULL;
        aMenu->firstItem = newItem;
    } else {
        if (aPosition >= aMenu->length) {
            newItem->previousItem = _menu_find_last_item(aMenu);
            newItem->nextItem = NULL;
            newItem->previousItem->nextItem = newItem;
        } else {
            newItem->nextItem = _menu_find_item(aMenu, aPosition);
            newItem->previousItem = newItem->nextItem->previousItem; // Copy pointer to previous item to new item
            if (newItem->nextItem != NULL) newItem->nextItem->previousItem = newItem; // Replace pointer to previous item with new item
            if (newItem->previousItem != NULL) newItem->previousItem->nextItem = newItem; // Replace pointer to next item in previous item
        }
    }
    aMenu->length++;
    return true;
}

bool menu_remove_item(menu_t* aMenu, size_t aPosition) {
    if (aMenu == NULL) return false; // Can't delete an item from a menu that doesn't exist
    if (aMenu->length <= aPosition) return false; // Can't delete an item that doesn't exist
    menu_item_t* item;
    
    if (aPosition == 0) {
        item = aMenu->firstItem;
        if (item == NULL) return false; // Can't delete if no linked list is allocated
        if (item->nextItem != NULL) {
            aMenu->firstItem = item->nextItem;
            aMenu->firstItem->previousItem = NULL;
        } else {
            aMenu->firstItem = NULL;
        }
    } else {
        item = _menu_find_item(aMenu, aPosition);
        if (item == NULL) return false;
        if (item->previousItem != NULL) item->previousItem->nextItem = item->nextItem;
        if (item->nextItem != NULL) item->nextItem->previousItem = item->previousItem;
    }
    free(item->label);
    free(item);
    aMenu->length--;
    return true;
}

bool menu_navigate_to(menu_t* aMenu, size_t aPosition) {
    if (aMenu == NULL) return false;
    if (aMenu->length < 1) return false;
    aMenu->position = aPosition;
    if (aMenu->position >= aMenu->length) aMenu->position = aMenu->length - 1;
    return true;
}

void menu_navigate_previous(menu_t* aMenu) {
    if (aMenu == NULL) return;
    if (aMenu->length < 1) return;
    aMenu->position--;
    if (aMenu->position > aMenu->length) {
       aMenu->position = aMenu->length - 1;
    }
}

void menu_navigate_next(menu_t* aMenu) {
    if (aMenu == NULL) return;
    if (aMenu->length < 1) return;
    aMenu->position = (aMenu->position + 1) % aMenu->length;
}

size_t menu_get_position(menu_t* aMenu) {
    return aMenu->position;
}

size_t menu_get_length(menu_t* aMenu) {
    return aMenu->length;
}

void* menu_get_callback_args(menu_t* aMenu, size_t aPosition) {
    menu_item_t* item = _menu_find_item(aMenu, aPosition);
    if (item == NULL) return NULL;
    return item->callbackArgs;
}

void menu_debug(menu_t* aMenu) {
    if (aMenu == NULL) {
        printf("Menu pointer is NULL\n");
        return;
    }
    printf("Title:    %s\n", aMenu->title);
    printf("Length:   %u\n", aMenu->length);
    printf("Position: %u\n", aMenu->position);
    menu_item_t* item = aMenu->firstItem;
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

void menu_render(pax_buf_t *aBuffer, menu_t* aMenu, float aPosX, float aPosY, float aWidth, float aHeight) {
    pax_col_t fgColor = 0xFF000000;
    pax_col_t bgColor = 0xFFFFFFFF;
    pax_col_t borderColor = 0xFF000000;
    pax_col_t titleColor = 0xFFFFFFFF;
    pax_col_t scrollbarBgColor = 0xFF555555;
    pax_col_t scrollbarFgColor = 0xFFCCCCCC;
    pax_col_t scrollbarSlColor = 0xFFFFFFFF;
    float  entry_height = 18 + 2;
    size_t maxItems = aHeight / entry_height;
    
    float posY = aPosY;
    
    pax_clip(aBuffer, aPosX, aPosY, aWidth, aHeight);
    pax_simple_rect(aBuffer, bgColor, aPosX, aPosY, aWidth, aHeight);
    
    if (maxItems > 1) {
        maxItems--;
        pax_simple_rect(aBuffer, borderColor, aPosX, posY, aWidth, entry_height);
        pax_simple_line(aBuffer, titleColor, aPosX + 1, aPosY + entry_height, aPosX + aWidth - 2, aPosY + entry_height - 1);
        pax_clip(aBuffer, aPosX + 1, posY + 1, aWidth - 2, entry_height - 2);
        pax_draw_text(aBuffer, titleColor, NULL, entry_height - 2, aPosX + 1, posY + 1, aMenu->title);
        posY += entry_height;
    }
    pax_clip(aBuffer, aPosX, posY, aWidth, aHeight);
    pax_outline_rect(aBuffer, borderColor, aPosX, aPosY, aWidth, aHeight);
    
    size_t itemOffset = 0;
    if (aMenu->position >= maxItems) {
        itemOffset = aMenu->position - maxItems + 1;
    }

    for (size_t index = itemOffset; (index < itemOffset + maxItems) && (index < aMenu->length); index++) {
        menu_item_t* item = _menu_find_item(aMenu, index);
        if (item == NULL) {
            printf("Render error: item is NULL at %u\n", index);
            break;
        }
        pax_clip(aBuffer, aPosX, posY, aWidth, entry_height);
        if (index == aMenu->position) {
            pax_simple_rect(aBuffer, fgColor, aPosX + 1, posY, aWidth - 2, entry_height);
            pax_clip(aBuffer, aPosX + 1, posY + 1, aWidth - 4, entry_height - 2);
            pax_draw_text(aBuffer, bgColor, NULL, entry_height - 2, aPosX + 1, posY + 1, item->label);
        } else {
            pax_simple_rect(aBuffer, bgColor, aPosX + 1, posY, aWidth - 2, entry_height);
            pax_clip(aBuffer, aPosX + 1, posY + 1, aWidth - 4, entry_height - 2);
            pax_draw_text(aBuffer, fgColor, NULL, entry_height - 2, aPosX + 1, posY + 1, item->label);
        }
        posY += entry_height;
    }
    
    pax_clip(aBuffer, aPosX + aWidth - 5, aPosY + entry_height, 4, aHeight - 1 - entry_height);
    
    float fractionStart = itemOffset / (aMenu->length * 1.0);
    float fractionSelected = aMenu->position / (aMenu->length * 1.0);
    float fractionEnd = (itemOffset + maxItems) / (aMenu->length * 1.0);
    if (fractionEnd > 1.0) fractionEnd = 1.0;
    
    float scrollbarHeight = aHeight - entry_height;
    float scrollbarStart = scrollbarHeight * fractionStart;
    float scrollbarSelected = scrollbarHeight * fractionSelected;
    float scrollbarEnd = scrollbarHeight * fractionEnd;
    
    pax_simple_rect(aBuffer, scrollbarBgColor, aPosX + aWidth - 5, aPosY + entry_height - 1, 4, scrollbarHeight);
    pax_simple_rect(aBuffer, scrollbarFgColor, aPosX + aWidth - 5, aPosY + entry_height - 1 + scrollbarStart, 4, scrollbarEnd - scrollbarStart);
    pax_simple_rect(aBuffer, scrollbarSlColor, aPosX + aWidth - 5, aPosY + entry_height - 1 + scrollbarSelected, 4, scrollbarHeight / aMenu->length);

    pax_noclip(aBuffer);
}
