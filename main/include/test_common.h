#pragma once

#include <stdio.h>
#include <string.h>

typedef bool (*test_fn)(uint32_t *rc);

bool test_wait_for_response(uint32_t *rc);
bool run_test(const pax_font_t *font, int line, const char *test_name, test_fn fn);

#define RUN_TEST(name, fn)                      \
    do {                                        \
        ok &= run_test(font, line++, name, fn); \
    } while (0)

#define RUN_TEST_MANDATORY(name, fn)                                                  \
    do {                                                                              \
        if (!run_test(font, line++, name, fn)) {                                      \
            pax_draw_text(pax_buffer, 0xffff0000, font, 18, 0, 20 * line, "Aborted"); \
            display_flush();                                                          \
            ok = false;                                                               \
            goto error;                                                               \
        }                                                                             \
    } while (0)

#define RUN_TEST_BLIND(name, fn)                \
    do {                                        \
        ok &= run_test(font, line++, name, fn); \
    } while (0)
