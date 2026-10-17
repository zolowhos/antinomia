// test_cpu.c - CPU pane model and layout without a live terminal.
// Module: test (C11).
// Owns: assertions for refresh, focus cycling, and screen compose.
// Depends: re_test, re_cpu, re_dbg, re_screen, re_arena.
#include "re_test.h"

#include "features/debug/re_dbg.h"
#include "utils/mem/re_arena.h"
#include "utils/text/re_strbuf.h"
#include "utils/tui/re_screen.h"
#include "cli/screen/menu/re_gui_menu.h"
#include "cli/screen/re_cpu.h"

#include <string.h>

int re_test_count = 0;
int re_test_fail = 0;

static void test_focus_cycles(void) {
    re_cpu_view_t v;
    unsigned i;
    re_cpu_init(&v);
    RE_CHECK_EQ_U(v.focus, RE_CPU_PANE_DISASM);
    for (i = 0; i < (unsigned)RE_CPU_PANE_COUNT; i++)
        re_cpu_focus_next(&v);
    RE_CHECK_EQ_U(v.focus, RE_CPU_PANE_DISASM);
    re_cpu_focus_prev(&v);
    RE_CHECK_EQ_U(v.focus, RE_CPU_PANE_TRACE);
    RE_CHECK_EQ_U((unsigned)RE_CPU_PANE_LOG + 1u, (unsigned)RE_CPU_PANE_TRACE);
}

static void test_refresh_detached(void) {
    re_cpu_view_t v;
    re_dbg_session_t s;
    re_cpu_init(&v);
    re_dbg_session_init(&s);
    re_cpu_refresh(&v, &s);
    RE_CHECK(!v.attached);
    RE_CHECK(v.n_disasm > 0);
    RE_CHECK(v.n_regs > 0);
    RE_CHECK(v.n_dump > 0);
    RE_CHECK(v.n_stack > 0);
    RE_CHECK(v.n_maps > 0);
    RE_CHECK(v.n_threads > 0);
    RE_CHECK(v.n_bp_lines > 0);
    RE_CHECK(v.n_log > 0);
    RE_CHECK(v.n_trace > 0);
    RE_CHECK(strstr(v.status_left, "pid=") != NULL);
    RE_CHECK(strstr(v.bp_lines[0].text, "no breakpoints") != NULL);
    re_dbg_session_free(&s);
}

static void set_bp_note(re_cpu_bp_t *bp, const char *n) {
    size_t i = 0;
    while (n[i] && i + 1u < sizeof(bp->note)) {
        bp->note[i] = n[i];
        i++;
    }
    bp->note[i] = 0;
}

static void test_bps_and_log(void) {
    re_cpu_view_t v;
    re_cpu_bp_t bp;
    re_dbg_session_t s;
    re_cpu_init(&v);
    re_dbg_session_init(&s);
    memset(&bp, 0, sizeof(bp));
    bp.addr = 0x401000;
    bp.enabled = true;
    set_bp_note(&bp, "entry");
    re_cpu_set_bps(&v, &bp, 1);
    s.last_event.kind = RE_DBG_EV_BREAK;
    s.last_event.pid = 9;
    s.last_event.tid = 9;
    s.last_event.addr = 0x401000;
    re_cpu_refresh(&v, &s);
    RE_CHECK(strstr(v.bp_lines[0].text, "401000") != NULL);
    RE_CHECK(strstr(v.log[v.n_log - 1].text, "break") != NULL);
    re_dbg_session_free(&s);
}

static void test_paint_zones(void) {
    re_arena_t a;
    re_strbuf_t out;
    re_screen_t s;
    re_cpu_view_t v;
    re_dbg_session_t sess;
    unsigned i;
    re_arena_init(&a, 0);
    re_strbuf_init(&out, &a);
    RE_CHECK(re_screen_init(&s, &a, &out, 40, 100));
    re_cpu_init(&v);
    re_dbg_session_init(&sess);
    re_cpu_refresh(&v, &sess);
    re_cpu_paint(&s, &v);
    for (i = 0; i < (unsigned)RE_CPU_PANE_COUNT; i++)
        RE_CHECK(v.zones[i] != RE_SCREEN_ZONE_NONE);
    RE_CHECK(re_cpu_focus_zone(&v, v.zones[RE_CPU_PANE_STACK]));
    RE_CHECK_EQ_U(v.focus, RE_CPU_PANE_STACK);
    re_strbuf_clear(&out);
    re_screen_dump(&s, &out);
    RE_CHECK(strstr(out.p, "disasm") != NULL);
    RE_CHECK(strstr(out.p, "registers") != NULL);
    RE_CHECK(strstr(out.p, "dump") != NULL);
    RE_CHECK(strstr(out.p, "call stack") != NULL || strstr(out.p, "stack") != NULL);
    RE_CHECK(strstr(out.p, "maps") != NULL);
    RE_CHECK(strstr(out.p, "threads") != NULL);
    RE_CHECK(strstr(out.p, "breakpoints") != NULL);
    RE_CHECK(strstr(out.p, "log") != NULL);
    re_dbg_session_free(&sess);
    re_arena_free(&a);
}

static void test_chrome_menus(void) {
    re_arena_t a;
    re_strbuf_t out;
    re_screen_t s;
    re_cpu_view_t v;
    re_gui_menu_ctx_t mc;
    size_t focus = 0;
    bool repaint = false, quit = false, color = true;
    int open = -1;
    uint8_t zone = RE_SCREEN_ZONE_NONE, nitems = 0, btn_zone = RE_SCREEN_ZONE_NONE, btn_n = 0;
    re_act_t pending = RE_ACT_NONE;
    re_arena_init(&a, 0);
    re_strbuf_init(&out, &a);
    RE_CHECK(re_screen_init(&s, &a, &out, 40, 100));
    re_cpu_init(&v);
    re_cpu_paint(&s, &v);
    mc.focus = &focus;
    mc.n_panes = (size_t)RE_CPU_PANE_COUNT;
    mc.repaint = &repaint;
    mc.quit = &quit;
    mc.color = &color;
    mc.open = &open;
    mc.bar = RE_SCREEN_ZONE_NONE;
    mc.zone = &zone;
    mc.nitems = &nitems;
    mc.pending = &pending;
    mc.log_fn = NULL;
    mc.log_user = NULL;
    mc.path = "antinomia";
    mc.mark = "antinomia 0.1.0";
    mc.btn_zone = &btn_zone;
    mc.btn_n = &btn_n;
    re_gui_chrome_draw(&s, &mc);
    RE_CHECK(mc.bar != RE_SCREEN_ZONE_NONE);
    RE_CHECK(btn_zone != RE_SCREEN_ZONE_NONE);
    RE_CHECK(btn_n == 5);
    re_strbuf_clear(&out);
    re_screen_dump(&s, &out);
    RE_CHECK(strstr(out.p, "File") != NULL);
    RE_CHECK(strstr(out.p, "Edit") != NULL);
    RE_CHECK(strstr(out.p, "Help") != NULL);
    RE_CHECK(strstr(out.p, "Run") != NULL);
    RE_CHECK(strstr(out.p, "disasm") != NULL);
    re_arena_free(&a);
}

int main(void) {
    test_focus_cycles();
    test_refresh_detached();
    test_bps_and_log();
    test_paint_zones();
    test_chrome_menus();
    return re_test_report("cpu");
}
