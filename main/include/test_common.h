#pragma once

#include <stdio.h>
#include <string.h>
#include "ili9341.h"
#include "pax_gfx.h"

typedef bool (*test_fn)(uint32_t *rc);

bool test_wait_for_response(uint32_t *rc);
bool run_test(pax_buf_t* pax_buffer, const pax_font_t *font, ILI9341* ili9341, int line, const char *test_name, test_fn fn);

#define RUN_TEST(name, fn) do {\
    ok &= run_test(pax_buffer, font, ili9341, line++, name, fn); \
} while (0)

#define RUN_TEST_MANDATORY(name, fn) do {\
    if (!run_test(pax_buffer, font, ili9341, line++, name, fn)) { \
        pax_draw_text(pax_buffer, 0xffff0000, font, 18, 0, 20*line, "Aborted"); \
        ili9341_write(ili9341, pax_buffer->buf); \
        ok = false; \
        goto error; \
    } \
} while (0)

#define RUN_TEST_BLIND(name, fn) do {\
    ok &= run_test(pax_buffer, font, NULL, line++, name, fn); \
} while (0)
