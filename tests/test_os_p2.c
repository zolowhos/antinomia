// test_os_p2.c - Phase 2 Wave 4 OS extras and competitive demo coverage.
// Module: test (C11).
// Owns: signal disposition, build-id modules, SEH/handles stubs, demo path.
// Depends: re_dbg, re_dbg_os, re_test. Live Linux path needs loop_inferior.
#if defined(__linux__)
#    define _GNU_SOURCE
#endif
#include "features/bp/re_dbg_advbp.h"
#include "features/debug/re_dbg.h"
#include "features/os/re_dbg_os.h"
#include "features/trace/re_dbg_trace.h"
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

static void test_seh_handles_unsupported_or_empty(void) {
    re_dbg_session_t s;
    re_dbg_seh_frame_t seh[8];
    re_dbg_handle_t h[8];
    size_t n = 0;
    re_err_code_t c;
    re_dbg_session_init(&s);
    c = re_dbg_session_enum_seh(&s, seh, 8, &n);
#if defined(_WIN32)
    RE_CHECK(c == RE_E_USAGE || c == RE_OK);
#else
    RE_CHECK_EQ_U(c, RE_E_UNSUPPORTED);
#endif
    c = re_dbg_session_enum_handles(&s, h, 8, &n);
#if defined(_WIN32)
    RE_CHECK(c == RE_E_USAGE || c == RE_OK);
#else
    RE_CHECK_EQ_U(c, RE_E_UNSUPPORTED);
#endif
    re_dbg_session_free(&s);
}

#if defined(__linux__)
static void test_signals_and_build_id(void) {
    re_dbg_session_t s;
    re_dbg_event_t ev;
    re_dbg_sig_disp_t sigs[RE_DBG_SIG_MAX];
    re_dbg_module_t mods[64];
    size_t n = 0, nm = 0, i;
    bool saw_build = false;
    bool saw_segv = false;
    re_dbg_session_init(&s);
    RE_CHECK_EQ_U(re_dbg_session_launch(&s, g_inferior, NULL), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_CREATE);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_BREAK);
    RE_CHECK_EQ_U(re_dbg_session_enum_signals(&s, sigs, RE_DBG_SIG_MAX, &n), RE_OK);
    RE_CHECK(n >= 32);
    for (i = 0; i < n; i++) {
        if (sigs[i].signo == 11) {
            saw_segv = true;
            RE_CHECK(sigs[i].name[0] != 0);
            RE_CHECK(sigs[i].disposition[0] != 0);
        }
    }
    RE_CHECK(saw_segv);
    RE_CHECK_EQ_U(re_dbg_session_enum_modules(&s, mods, 64, &nm), RE_OK);
    RE_CHECK(nm >= 1);
    for (i = 0; i < nm; i++) {
        if (mods[i].build_id[0])
            saw_build = true;
    }
    /* At least the main image or libc usually carries a GNU build-id. */
    RE_CHECK(saw_build);
    RE_CHECK_EQ_U(re_dbg_session_kill(&s), RE_OK);
    re_dbg_session_free(&s);
}

static void test_phase2_demo_path(void) {
    re_dbg_session_t s;
    re_dbg_event_t ev;
    re_dbg_frame_t frames[16];
    re_dbg_symbol_t info;
    size_t nf = 0;
    uint64_t touch = 0, watch = 0;
    char trace_path[] = "/tmp/antinomia_phase2_trace.json";
    re_dbg_session_init(&s);
    RE_CHECK_EQ_U(re_dbg_session_launch(&s, g_inferior, NULL), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_CREATE);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_BREAK);
    /* Soft BP + log + HW watch first; r_debug is often null this early. */
    RE_CHECK_EQ_U(re_dbg_session_resolve(&s, "loop_inferior:antinomia_touch", &touch, &info),
                  RE_OK);
    RE_CHECK(touch != 0);
    RE_CHECK_EQ_U(re_dbg_session_bp_set(&s, touch), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_bp_set_cond(&s, touch, "break", "1"), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_bp_set_cond(&s, touch, "log", "demo {rax}"), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_resolve(&s, "loop_inferior:antinomia_watch", &watch, &info),
                  RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_hw_bp_set(&s, watch, 4, RE_DBG_BP_AX_W), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_cont(&s), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 5000), RE_OK);
    RE_CHECK(ev.kind == RE_DBG_EV_BREAK || ev.kind == RE_DBG_EV_LOAD_MODULE);
    /* After linker init, module-load BP arming (bpdll) should succeed. */
    RE_CHECK_EQ_U(re_dbg_session_load_bp_set(&s, "libm"), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_bt(&s, frames, 16, &nf), RE_OK);
    RE_CHECK(nf >= 1);
    RE_CHECK_EQ_U(re_dbg_session_trace_start(&s), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_trace_step(&s, 4), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_trace_save(&s, trace_path), RE_OK);
    RE_CHECK_EQ_U(re_dbg_db_add_bookmark(&s.db, 0, "phase2_demo"), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_db_save(&s), RE_OK);
    RE_CHECK(s.db.db_path[0] != 0);
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
    test_seh_handles_unsupported_or_empty();
#if defined(__linux__)
    if (!g_inferior) {
        printf("FAIL missing RE_TEST_INFERIOR / argv[1]\n");
        return 1;
    }
    test_signals_and_build_id();
    test_phase2_demo_path();
#else
    {
        re_dbg_sig_disp_t sigs[4];
        size_t n = 0;
        re_dbg_session_t s;
        re_dbg_session_init(&s);
        RE_CHECK_EQ_U(re_dbg_session_enum_signals(&s, sigs, 4, &n), RE_E_UNSUPPORTED);
        re_dbg_session_free(&s);
    }
#endif
    return re_test_report("os_p2");
}
