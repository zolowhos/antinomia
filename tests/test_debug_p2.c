// test_debug_p2.c - Phase 2 Wave 1 caps, exception policy, and advbp coverage.
// Module: test (C11).
// Owns: capability bit checks, exception rules, HW/mem/load BP live path.
// Depends: re_dbg, re_dbg_advbp, re_test. Live HW path needs loop_inferior.
#if defined(__linux__)
#    define _GNU_SOURCE
#endif
#include "features/bp/re_dbg_advbp.h"
#include "features/debug/re_dbg.h"
#include "re_test.h"

#include <stdio.h>
#include <string.h>

#if defined(__linux__)
#    include <stdlib.h>
#endif

int re_test_count = 0;
int re_test_fail = 0;

#if defined(__linux__)
static const char *g_inferior;
#endif

static void test_exception_policy_and_caps(void) {
    re_dbg_session_t s;
    re_dbg_ex_rule_t rules[8];
    size_t n = 0;
    re_dbg_caps_t caps;
    re_dbg_session_init(&s);
    caps = re_dbg_session_caps(&s);
#if defined(__linux__) || defined(_WIN32)
    RE_CHECK((caps.bits & RE_DBG_CAP_SW_BP) != 0);
    RE_CHECK((caps.bits & RE_DBG_CAP_HW_BP) != 0);
    RE_CHECK((caps.bits & RE_DBG_CAP_MEM_BP) != 0);
    RE_CHECK((caps.bits & RE_DBG_CAP_LOAD_BP) != 0);
    RE_CHECK((caps.bits & RE_DBG_CAP_EXCEPTION_BP) != 0);
    RE_CHECK((caps.bits & RE_DBG_CAP_STEP) != 0);
    RE_CHECK_EQ_U(caps.hw_slots, 4u);
#endif
#if defined(__linux__)
    RE_CHECK((caps.bits & RE_DBG_CAP_SIGNAL_PASS) != 0);
#elif defined(_WIN32)
    RE_CHECK((caps.bits & RE_DBG_CAP_FIRST_CHANCE) != 0);
#endif
    RE_CHECK_EQ_U(re_dbg_session_ex_set(&s, 11, 11, RE_DBG_EX_IGNORE, true), RE_OK); /* SIGSEGV */
    RE_CHECK_EQ_U(re_dbg_session_ex_set(&s, 4, 8, RE_DBG_EX_PASS, true), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_ex_set(&s, 0xC0000005, 0xC0000005, RE_DBG_EX_BREAK_PASS, true),
                  RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_ex_list(&s, rules, 8, &n), RE_OK);
    RE_CHECK_EQ_U(n, 3);
    RE_CHECK(strcmp(re_dbg_ex_policy_str(rules[0].policy), "ignore") == 0);
    RE_CHECK_EQ_U(re_dbg_session_ex_clear(&s, 4, 8), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_ex_list(&s, rules, 8, &n), RE_OK);
    RE_CHECK_EQ_U(n, 2);
    RE_CHECK(RE_DBG_EV_BREAK == RE_DBG_EV_BREAKPOINT);
    re_dbg_session_free(&s);
}

#if defined(__linux__)
static void test_caps_hw_mem_load_bp(void) {
    re_dbg_session_t s;
    re_dbg_event_t ev;
    re_dbg_caps_t caps;
    re_dbg_regs_t regs;
    re_dbg_bp_ex_t list[16];
    re_dbg_symbol_t info;
    size_t n = 0;
    uint64_t watch = 0;
    uint64_t rend = 0;
    re_dbg_session_init(&s);
    caps = re_dbg_session_caps(&s);
    RE_CHECK((caps.bits & RE_DBG_CAP_HW_BP) != 0);
    RE_CHECK((caps.bits & RE_DBG_CAP_MEM_BP) != 0);
    RE_CHECK((caps.bits & RE_DBG_CAP_LOAD_BP) != 0);
    RE_CHECK_EQ_U(caps.hw_slots, 4u);
    RE_CHECK_EQ_U(re_dbg_session_launch(&s, g_inferior, NULL), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_CREATE);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_BREAK);
    RE_CHECK_EQ_U(re_dbg_session_get_regs(&s, 0, &regs), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_hw_bp_set(&s, regs.rip, 1, RE_DBG_BP_AX_X), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_bp_list_ex(&s, list, 16, &n), RE_OK);
    RE_CHECK(n >= 1);
    RE_CHECK_EQ_U(re_dbg_session_cont(&s), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_BREAK);
    RE_CHECK_EQ_U(ev.addr, regs.rip);
    RE_CHECK_EQ_U(re_dbg_session_hw_bp_clear(&s, regs.rip), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_resolve(&s, "loop_inferior:antinomia_watch", &watch, &info),
                  RE_OK);
    RE_CHECK(watch != 0);
    RE_CHECK_EQ_U(re_dbg_session_mem_bp_set(&s, watch, 4, RE_DBG_BP_AX_W), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_cont(&s), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 3000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_BREAK);
    RE_CHECK_EQ_U(ev.addr, watch);
    RE_CHECK_EQ_U(re_dbg_session_mem_bp_clear(&s, watch), RE_OK);
    RE_CHECK_EQ_U(s.backend->vt->rendezvous_addr(s.backend, &rend), RE_OK);
    RE_CHECK(rend != 0);
    RE_CHECK_EQ_U(re_dbg_session_load_bp_set(&s, "libm"), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_bp_list_ex(&s, list, 16, &n), RE_OK);
    {
        size_t i;
        bool saw_load = false;
        for (i = 0; i < n; i++) {
            if (list[i].kind == RE_DBG_BP_KIND_LOAD)
                saw_load = true;
        }
        RE_CHECK(saw_load);
    }
    RE_CHECK_EQ_U(re_dbg_session_kill(&s), RE_OK);
    re_dbg_session_free(&s);
}

static void test_maps_threads_stack(void) {
    re_dbg_session_t s;
    re_dbg_event_t ev;
    re_dbg_map_t maps[64];
    re_dbg_thread_t thr[16];
    re_dbg_frame_t frames[16];
    size_t n = 0, nt = 0, nf = 0;
    uint32_t tid0 = 0;
    char prot[4];
    re_dbg_session_init(&s);
    RE_CHECK_EQ_U(re_dbg_session_launch(&s, g_inferior, NULL), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_CREATE);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_BREAK);
    RE_CHECK_EQ_U(re_dbg_session_enum_maps(&s, maps, 64, &n), RE_OK);
    RE_CHECK(n >= 1);
    RE_CHECK(maps[0].base != 0);
    re_dbg_map_prot_str(maps[0].protect, prot);
    RE_CHECK(prot[0] == 'r' || prot[0] == '-');
    RE_CHECK_EQ_U(re_dbg_session_follow_dump(&s, maps[0].base), RE_OK);
    RE_CHECK_EQ_U(s.follow_dump, maps[0].base);
    RE_CHECK_EQ_U(re_dbg_session_enum_threads(&s, thr, 16, &nt), RE_OK);
    RE_CHECK(nt >= 1);
    tid0 = thr[0].tid;
    RE_CHECK_EQ_U(re_dbg_session_thread_switch(&s, tid0), RE_OK);
    RE_CHECK_EQ_U(s.tid, tid0);
    RE_CHECK_EQ_U(re_dbg_session_thread_suspend(&s, tid0), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_enum_threads(&s, thr, 16, &nt), RE_OK);
    RE_CHECK(thr[0].suspended);
    RE_CHECK_EQ_U(re_dbg_session_thread_resume(&s, tid0), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_enum_threads(&s, thr, 16, &nt), RE_OK);
    RE_CHECK(!thr[0].suspended);
    RE_CHECK_EQ_U(re_dbg_session_bt(&s, frames, 16, &nf), RE_OK);
    RE_CHECK(nf >= 1);
    RE_CHECK(frames[0].ip != 0);
    /* Module annotate expected for the stopped inferior image. */
    RE_CHECK(frames[0].module[0] != 0);
    RE_CHECK_EQ_U(re_dbg_session_kill(&s), RE_OK);
    re_dbg_session_free(&s);
}
#endif

int main(int argc, char **argv) {
#if defined(__linux__)
    g_inferior = getenv("RE_TEST_INFERIOR");
    if (!g_inferior && argc > 1)
        g_inferior = argv[1];
#else
    (void)argc;
    (void)argv;
#endif
    test_exception_policy_and_caps();
#if defined(__linux__)
    if (!g_inferior) {
        printf("FAIL missing RE_TEST_INFERIOR / argv[1] path to loop_inferior\n");
        return 1;
    }
    test_caps_hw_mem_load_bp();
    test_maps_threads_stack();
#endif
    return re_test_report("debug_p2");
}
