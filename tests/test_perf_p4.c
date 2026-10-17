// test_perf_p4.c - Phase 4 Pack C: disasm cache, dirty panes, event coalesce.
// Module: test (C11).
// Owns: unit assertions for the three Pack C performance helpers.
// Depends: re_test, re_disasm, re_dbg_coalesce, re_cpu, re_screen, re_arena.
#include "re_test.h"

#include "cli/screen/re_cpu.h"
#include "features/disasm/re_disasm.h"
#include "features/sess/re_dbg_coalesce.h"
#include "utils/mem/re_arena.h"
#include "utils/text/re_strbuf.h"
#include "utils/tui/re_screen.h"

#include <string.h>

int re_test_count = 0;
int re_test_fail = 0;

static void test_disasm_cache_hit(void) {
    re_disasm_cache_t c;
    uint8_t code[16];
    uint32_t hits = 0, misses = 0, inv = 0;
    memset(code, 0x90, sizeof(code));
    re_disasm_cache_clear(&c);
    RE_CHECK_EQ_U(re_disasm_cache_fill(&c, 0x401000, code, sizeof(code), 8), RE_OK);
    RE_CHECK(c.valid);
    RE_CHECK_EQ_U(c.n > 0, 1);
    re_disasm_cache_stats(&c, &hits, &misses, &inv);
    RE_CHECK_EQ_U(misses, 1);
    RE_CHECK_EQ_U(hits, 0);
    RE_CHECK_EQ_U(re_disasm_cache_fill(&c, 0x401000, code, sizeof(code), 8), RE_OK);
    re_disasm_cache_stats(&c, &hits, &misses, &inv);
    RE_CHECK_EQ_U(hits, 1);
    RE_CHECK_EQ_U(misses, 1);
    code[0] = 0xcc;
    RE_CHECK_EQ_U(re_disasm_cache_fill(&c, 0x401000, code, sizeof(code), 8), RE_OK);
    re_disasm_cache_stats(&c, &hits, &misses, &inv);
    RE_CHECK_EQ_U(misses, 2);
    re_disasm_cache_invalidate(&c, 0x401000, 1);
    re_disasm_cache_stats(&c, &hits, &misses, &inv);
    RE_CHECK_EQ_U(inv, 1);
    RE_CHECK(!c.valid);
}

static void test_disasm_cache_slots(void) {
    re_disasm_cache_t c;
    uint8_t a[32], b[32];
    size_t i;
    memset(a, 0x90, sizeof(a));
    memset(b, 0xcc, sizeof(b));
    re_disasm_cache_clear(&c);
    for (i = 0; i < RE_DISASM_CACHE_SLOTS; i++) {
        RE_CHECK_EQ_U(
            re_disasm_cache_fill(&c, 0x1000u + i * 0x100u, a, sizeof(a), 4), RE_OK);
    }
    RE_CHECK_EQ_U(re_disasm_cache_fill(&c, 0x1000u, a, sizeof(a), 4), RE_OK);
    RE_CHECK(c.hits >= 1);
    RE_CHECK_EQ_U(re_disasm_cache_fill(&c, 0x9000u, b, sizeof(b), 4), RE_OK);
    RE_CHECK(c.misses >= RE_DISASM_CACHE_SLOTS);
}

static void test_coalesce_soft_runs(void) {
    re_dbg_event_t evs[6];
    uint32_t dropped = 0;
    size_t n;
    memset(evs, 0, sizeof(evs));
    evs[0].kind = RE_DBG_EV_THREAD_CREATE;
    evs[0].tid = 1;
    evs[1].kind = RE_DBG_EV_THREAD_CREATE;
    evs[1].tid = 2;
    evs[2].kind = RE_DBG_EV_THREAD_CREATE;
    evs[2].tid = 3;
    evs[3].kind = RE_DBG_EV_BREAKPOINT;
    evs[3].addr = 0x401000;
    evs[4].kind = RE_DBG_EV_LOAD_MODULE;
    evs[4].addr = 0x7f0000;
    evs[5].kind = RE_DBG_EV_LOAD_MODULE;
    evs[5].addr = 0x7f1000;
    RE_CHECK(re_dbg_event_is_soft(RE_DBG_EV_THREAD_CREATE));
    RE_CHECK(!re_dbg_event_is_soft(RE_DBG_EV_BREAKPOINT));
    n = re_dbg_coalesce_events(evs, 6, &dropped);
    RE_CHECK_EQ_U(n, 3);
    RE_CHECK_EQ_U(dropped, 3);
    RE_CHECK_EQ_U(evs[0].kind, RE_DBG_EV_THREAD_CREATE);
    RE_CHECK_EQ_U(evs[0].tid, 3);
    RE_CHECK(evs[0].code >= 3);
    RE_CHECK_EQ_U(evs[1].kind, RE_DBG_EV_BREAKPOINT);
    RE_CHECK_EQ_U(evs[2].kind, RE_DBG_EV_LOAD_MODULE);
    RE_CHECK_EQ_U(evs[2].addr, 0x7f1000);
}

static void test_incremental_dirty(void) {
    re_cpu_view_t v;
    re_dbg_session_t s;
    re_arena_t a;
    re_strbuf_t out;
    re_screen_t scr;
    uint32_t filled_first;
    re_cpu_init(&v);
    re_dbg_session_init(&s);
    re_arena_init(&a, 0);
    re_strbuf_init(&out, &a);
    RE_CHECK(re_screen_init(&scr, &a, &out, 40, 100));
    RE_CHECK(v.paint_full);
    re_cpu_refresh(&v, &s);
    filled_first = v.last_filled;
    RE_CHECK((filled_first & RE_CPU_DIRTY_ALL) == RE_CPU_DIRTY_ALL);
    re_cpu_paint(&scr, &v);
    RE_CHECK(!v.paint_full);
    RE_CHECK_EQ_U(v.dirty, 0);
    /* Idle refresh: only status (and maybe log) should refill. */
    re_cpu_refresh(&v, &s);
    RE_CHECK((v.last_filled & (1u << (unsigned)RE_CPU_PANE_MAPS)) == 0);
    RE_CHECK((v.last_filled & (1u << (unsigned)RE_CPU_PANE_DISASM)) == 0);
    re_cpu_focus_next(&v);
    RE_CHECK(re_cpu_pane_dirty(&v, RE_CPU_PANE_DISASM));
    RE_CHECK(re_cpu_pane_dirty(&v, RE_CPU_PANE_REGS));
    re_cpu_refresh(&v, &s);
    re_cpu_paint(&scr, &v);
    RE_CHECK_EQ_U(v.dirty, 0);
    re_dbg_session_free(&s);
    re_arena_free(&a);
}

int main(void) {
    test_disasm_cache_hit();
    test_disasm_cache_slots();
    test_coalesce_soft_runs();
    test_incremental_dirty();
    return re_test_report("perf_p4");
}
