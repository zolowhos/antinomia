// test_robust_p4.c - Phase 4 Pack D: exit races, BP storms, large modules.
// Module: test (C11).
// Owns: unit assertions for Pack D robustness helpers (no live inferior).
// Depends: re_test, re_dbg, re_dbg_coalesce.
#include "re_test.h"

#include "features/debug/re_dbg.h"
#include "features/sess/re_dbg_coalesce.h"

#include <string.h>

int re_test_count = 0;
int re_test_fail = 0;

static void test_bp_storm_coalesce(void) {
    re_dbg_event_t evs[8];
    uint32_t dropped = 0;
    size_t n;
    memset(evs, 0, sizeof(evs));
    evs[0].kind = RE_DBG_EV_BREAKPOINT;
    evs[0].addr = 0x401000;
    evs[0].tid = 1;
    evs[1].kind = RE_DBG_EV_BREAKPOINT;
    evs[1].addr = 0x401000;
    evs[1].tid = 2;
    evs[2].kind = RE_DBG_EV_BREAKPOINT;
    evs[2].addr = 0x401000;
    evs[2].tid = 3;
    evs[3].kind = RE_DBG_EV_BREAKPOINT;
    evs[3].addr = 0x402000;
    evs[3].tid = 4;
    evs[4].kind = RE_DBG_EV_THREAD_CREATE;
    evs[4].tid = 5;
    evs[5].kind = RE_DBG_EV_THREAD_CREATE;
    evs[5].tid = 6;
    RE_CHECK(re_dbg_event_is_bp_storm(&evs[0], &evs[1]));
    RE_CHECK(!re_dbg_event_is_bp_storm(&evs[0], &evs[3]));
    n = re_dbg_coalesce_events(evs, 6, &dropped);
    RE_CHECK_EQ_U(n, 3);
    RE_CHECK_EQ_U(dropped, 3);
    RE_CHECK_EQ_U(evs[0].kind, RE_DBG_EV_BREAKPOINT);
    RE_CHECK_EQ_U(evs[0].addr, 0x401000);
    RE_CHECK_EQ_U(evs[0].tid, 1); /* first stopper kept for focus */
    RE_CHECK(evs[0].code >= 3);
    RE_CHECK_EQ_U(evs[1].kind, RE_DBG_EV_BREAKPOINT);
    RE_CHECK_EQ_U(evs[1].addr, 0x402000);
    RE_CHECK_EQ_U(evs[2].kind, RE_DBG_EV_THREAD_CREATE);
    RE_CHECK_EQ_U(evs[2].tid, 6);
}

static void test_exit_clears_attached(void) {
    re_dbg_session_t s;
    re_dbg_event_t ev;
    re_dbg_session_init(&s);
    s.attached = true;
    s.pid = 4242;
    s.tid = 4242;
    memset(&ev, 0, sizeof(ev));
    ev.kind = RE_DBG_EV_EXIT;
    ev.pid = 4242;
    ev.tid = 4242;
    ev.code = 7;
    /* Simulate post-wait EXIT apply without a live inferior. */
    s.last_event = ev;
    s.exit_code = ev.code;
    s.attached = false;
    RE_CHECK(!s.attached);
    RE_CHECK_EQ_U((unsigned)s.exit_code, 7u);
    RE_CHECK_EQ_U(re_dbg_session_cont(&s), RE_E_USAGE);
    RE_CHECK(strstr(s.last_err.msg, "inferior exited") != NULL ||
             strstr(s.last_err.msg, "not attached") != NULL);
    s.last_event.kind = RE_DBG_EV_EXIT;
    RE_CHECK_EQ_U(re_dbg_session_step(&s), RE_E_USAGE);
    RE_CHECK(strstr(s.last_err.msg, "inferior exited") != NULL);
    /* Never attached: USAGE. After EXIT: detach OK (idempotent). */
    re_dbg_session_free(&s);
    re_dbg_session_init(&s);
    RE_CHECK_EQ_U(re_dbg_session_detach(&s), RE_E_USAGE);
    RE_CHECK_EQ_U(re_dbg_session_kill(&s), RE_E_USAGE);
    s.last_event.kind = RE_DBG_EV_EXIT;
    s.exit_code = 0;
    RE_CHECK_EQ_U(re_dbg_session_detach(&s), RE_OK);
    re_dbg_session_free(&s);
}

static void test_module_capacity(void) {
    RE_CHECK(RE_DBG_MAX_MODULES >= 512);
    RE_CHECK(sizeof(re_dbg_module_t) > 0);
}

static void test_soft_and_bp_mix(void) {
    re_dbg_event_t evs[5];
    uint32_t dropped = 0;
    size_t n;
    memset(evs, 0, sizeof(evs));
    evs[0].kind = RE_DBG_EV_LOAD_MODULE;
    evs[0].addr = 0x1000;
    evs[1].kind = RE_DBG_EV_LOAD_MODULE;
    evs[1].addr = 0x2000;
    evs[2].kind = RE_DBG_EV_BREAKPOINT;
    evs[2].addr = 0x4000;
    evs[2].tid = 10;
    evs[3].kind = RE_DBG_EV_BREAKPOINT;
    evs[3].addr = 0x4000;
    evs[3].tid = 11;
    evs[4].kind = RE_DBG_EV_EXIT;
    evs[4].code = 0;
    n = re_dbg_coalesce_events(evs, 5, &dropped);
    RE_CHECK_EQ_U(n, 3);
    RE_CHECK_EQ_U(dropped, 2);
    RE_CHECK_EQ_U(evs[0].kind, RE_DBG_EV_LOAD_MODULE);
    RE_CHECK_EQ_U(evs[1].kind, RE_DBG_EV_BREAKPOINT);
    RE_CHECK(evs[1].code >= 2);
    RE_CHECK_EQ_U(evs[2].kind, RE_DBG_EV_EXIT);
}

int main(void) {
    test_bp_storm_coalesce();
    test_exit_clears_attached();
    test_module_capacity();
    test_soft_and_bp_mix();
    return re_test_report("robust_p4");
}
