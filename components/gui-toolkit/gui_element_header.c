#include "gui_element_header.h"

void render_header(pax_buf_t* pax_buffer, float position_x, float position_y, float width, float height, float text_height, pax_col_t text_color,
                   pax_col_t bg_color, pax_buf_t* icon, const char* label) {
    const pax_font_t* font = pax_font_saira_regular;
    pax_simple_rect(pax_buffer, bg_color, position_x, position_y, width, height);
    pax_clip(pax_buffer, position_x + 1, position_y + ((height - text_height) / 2) + 1, width - 2, text_height);
    pax_draw_text(pax_buffer, text_color, font, text_height, position_x + ((icon != NULL) ? 32 : 0) + 1, position_y + ((height - text_height) / 2) + 1, label);
    if (icon != NULL) {
        pax_clip(pax_buffer, position_x, position_y, 32, 32);
        pax_draw_image(pax_buffer, icon, position_x, position_y);
    }
    pax_noclip(pax_buffer);
}
