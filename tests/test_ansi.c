// test_ansi.c - Phase 4 terminal-matrix ANSI SGR smoke (no live TTY required).
// Module: test (C11).
// Owns: style-table ESC checks with colour on/off for CI matrix.
// Depends: re_tui. Prints to stdout (test binary OK).
#include "re_test.h"

#include <string.h>

#include "utils/mem/re_arena.h"
#include "utils/text/re_strbuf.h"
#include "utils/tui/re_tui.h"

int re_test_count = 0;
int re_test_fail = 0;

static void test_ansi_matrix(void) {
    re_arena_t a;
    re_strbuf_t out;
    re_strbuf_t scratch;
    re_tui_t t;
    const char *on;
    const char *s;
    re_arena_init(&a, 0);
    re_tui_styles_reset();
    on = re_tui_style_on(RE_ST_ACCENT);
    RE_CHECK(on && strstr(on, "\x1b[") != NULL);
    RE_CHECK(strstr(on, "\x1b[0m") == NULL);
    re_strbuf_init(&out, &a);
    re_strbuf_init(&scratch, &a);
    re_tui_init(&t, &out, &scratch, true, false, 40);
    re_tui_styled(&out, &t, RE_ST_GOOD, "ok");
    s = out.p ? out.p : "";
    RE_CHECK(strstr(s, "\x1b[32m") != NULL);
    RE_CHECK(strstr(s, "\x1b[0m") != NULL);
    re_strbuf_init(&out, &a);
    re_strbuf_init(&scratch, &a);
    re_tui_init(&t, &out, &scratch, false, false, 40);
    re_tui_styled(&out, &t, RE_ST_GOOD, "ok");
    s = out.p ? out.p : "";
    RE_CHECK(strchr(s, '\x1b') == NULL);
    RE_CHECK(strstr(s, "ok") != NULL);
    re_arena_free(&a);
}

int main(void) {
    test_ansi_matrix();
    return re_test_report("ansi");
}
