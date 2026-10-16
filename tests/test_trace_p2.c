// test_trace_p2.c - Phase 2 Pack G trace record, save/load, and paging.
// Module: test (C11).
// Owns: offline buffer tests plus Linux live tracestep against loop_inferior.
// Depends: re_dbg_trace, re_test. Live path needs RE_TEST_INFERIOR.
#if defined(__linux__)
#    define _GNU_SOURCE
#endif
#include "features/debug/re_dbg.h"
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

static void test_trace_save_load_paging(void) {
    re_dbg_session_t s;
    re_dbg_trace_frame_t frames[8];
    size_t n = 0, total = 0, i;
    bool trunc = false;
    char path[256];
    re_dbg_session_init(&s);
    RE_CHECK(s.trace != NULL);
    RE_CHECK(!re_dbg_session_trace_recording(&s));
    RE_CHECK_EQ_U(re_dbg_session_trace_start(&s), RE_OK);
    RE_CHECK(re_dbg_session_trace_recording(&s));
    /* Inject synthetic frames through the ring without a live target. */
    for (i = 0; i < 5; i++) {
        re_dbg_trace_frame_t f;
        memset(&f, 0, sizeof(f));
        f.index = s.trace->next_index;
        f.tid = 1;
        f.size = 1;
        f.bytes[0] = 0x90;
        memcpy(f.text, "nop", 4);
        f.regs.rip = 0x401000 + i;
        f.regs.rax = i;
        s.trace->frames[s.trace->head] = f;
        s.trace->head = (s.trace->head + 1u) % RE_TRACE_CAP;
        if (s.trace->count < RE_TRACE_CAP)
            s.trace->count++;
        s.trace->next_index++;
    }
    RE_CHECK_EQ_U(re_dbg_session_trace_count(&s), 5);
    RE_CHECK_EQ_U(re_dbg_session_trace_query(&s, 2, 2, frames, 8, &n, &total, &trunc), RE_OK);
    RE_CHECK_EQ_U(total, 5);
    RE_CHECK_EQ_U(n, 2);
    RE_CHECK(trunc);
    RE_CHECK_EQ_U(frames[0].regs.rip, 0x401002);
    RE_CHECK_EQ_U(frames[1].regs.rip, 0x401003);
    snprintf(path, sizeof(path), "%s/antinomia_trace_test.json",
#if defined(_WIN32)
             getenv("TEMP") ? getenv("TEMP") : "."
#else
             "/tmp"
#endif
    );
    RE_CHECK_EQ_U(re_dbg_session_trace_save(&s, path), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_trace_clear(&s), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_trace_count(&s), 0);
    RE_CHECK(!re_dbg_session_trace_recording(&s));
    RE_CHECK_EQ_U(re_dbg_session_trace_load(&s, path), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_trace_count(&s), 5);
    RE_CHECK_EQ_U(re_dbg_session_trace_query(&s, 0, 10, frames, 8, &n, &total, &trunc), RE_OK);
    RE_CHECK_EQ_U(n, 5);
    RE_CHECK_EQ_U(frames[4].regs.rax, 4);
    RE_CHECK(strcmp(frames[0].text, "nop") == 0);
    (void)remove(path);
    re_dbg_session_free(&s);
}

#if defined(__linux__)
static void test_trace_live_step(void) {
    re_dbg_session_t s;
    re_dbg_event_t ev;
    re_dbg_trace_frame_t frames[16];
    size_t n = 0, total = 0;
    bool trunc = false;
    if (!g_inferior || !g_inferior[0])
        return;
    re_dbg_session_init(&s);
    RE_CHECK_EQ_U(re_dbg_session_launch(&s, g_inferior, NULL), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_CREATE);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_BREAK);
    RE_CHECK_EQ_U(re_dbg_session_trace_step(&s, 3), RE_OK);
    RE_CHECK(re_dbg_session_trace_count(&s) >= 3);
    RE_CHECK_EQ_U(re_dbg_session_trace_query(&s, 0, 16, frames, 16, &n, &total, &trunc), RE_OK);
    RE_CHECK(n >= 3);
    RE_CHECK(frames[0].regs.rip != 0);
    RE_CHECK(frames[0].text[0] != 0);
    RE_CHECK_EQ_U(re_dbg_session_kill(&s), RE_OK);
    re_dbg_session_free(&s);
}
#endif

int main(int argc, char **argv) {
#if defined(__linux__)
    g_inferior = getenv("RE_TEST_INFERIOR");
    if ((!g_inferior || !g_inferior[0]) && argc > 1)
        g_inferior = argv[1];
#else
    (void)argc;
    (void)argv;
#endif
    test_trace_save_load_paging();
#if defined(__linux__)
    test_trace_live_step();
#endif
    return re_test_report("trace_p2");
}
