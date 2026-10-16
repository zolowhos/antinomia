// test_debug.c - debug backend session and Linux process-control checks.
// Module: test (C11).
// Owns: stub checks on Windows; launch/attach/detach/kill/wait on Linux.
// Depends: re_dbg, re_test. Prints to stdout, which is fine for a test binary.
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#include "features/debug/re_dbg.h"
#include "re_test.h"

#include <stdio.h>
#include <string.h>

#if defined(__linux__)
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

int re_test_count = 0;
int re_test_fail = 0;

static void test_session_lifecycle(void) {
    re_dbg_session_t s;
    re_dbg_session_init(&s);
    RE_CHECK(s.backend != NULL);
    RE_CHECK(s.backend->vt != NULL);
    RE_CHECK(s.backend->vt->kill != NULL);
    re_dbg_session_reset(&s);
    RE_CHECK(s.backend != NULL);
    re_dbg_session_free(&s);
}

#if !defined(__linux__)

static void test_stubs(void) {
    re_dbg_session_t s;
    re_dbg_event_t ev;
    re_dbg_regs_t regs;
    size_t n = 0;
    re_dbg_session_init(&s);
    RE_CHECK_EQ_U(s.backend->vt->launch(s.backend, "x", NULL), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(s.backend->vt->attach(s.backend, 1), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(s.backend->vt->detach(s.backend), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(s.backend->vt->kill(s.backend), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(s.backend->vt->wait_event(s.backend, &ev, 0), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(s.backend->vt->cont(s.backend), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(s.backend->vt->step(s.backend), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(s.backend->vt->get_regs(s.backend, 0, &regs), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(s.backend->vt->set_regs(s.backend, 0, &regs), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(s.backend->vt->read_mem(s.backend, 0, NULL, 0), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(s.backend->vt->write_mem(s.backend, 0, NULL, 0), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(s.backend->vt->enum_threads(s.backend, NULL, 0, &n), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(s.backend->vt->enum_modules(s.backend, NULL, 0, &n), RE_E_UNSUPPORTED);
    re_dbg_session_free(&s);
}

#else /* __linux__ */

static const char *g_inferior;

static re_err_code_t drain_create_break(re_dbg_backend_t *b, re_dbg_event_t *break_ev) {
    re_dbg_event_t ev;
    re_err_code_t c = b->vt->wait_event(b, &ev, 2000);
    if (c != RE_OK)
        return c;
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_CREATE);
    c = b->vt->wait_event(b, &ev, 2000);
    if (c != RE_OK)
        return c;
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_BREAK);
    if (break_ev)
        *break_ev = ev;
    return RE_OK;
}

static void test_launch_entry_step(void) {
    re_dbg_session_t s;
    re_dbg_event_t ev;
    re_dbg_regs_t regs;
    re_dbg_session_init(&s);
    RE_CHECK(g_inferior != NULL);
    RE_CHECK_EQ_U(s.backend->vt->launch(s.backend, g_inferior, NULL), RE_OK);
    RE_CHECK_EQ_U(drain_create_break(s.backend, &ev), RE_OK);
    RE_CHECK(ev.pid != 0);
    RE_CHECK_EQ_U(s.backend->vt->get_regs(s.backend, 0, &regs), RE_OK);
    RE_CHECK(regs.rip != 0);
    RE_CHECK_EQ_U(s.backend->vt->step(s.backend), RE_OK);
    RE_CHECK_EQ_U(s.backend->vt->wait_event(s.backend, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_STEP);
    RE_CHECK_EQ_U(s.backend->vt->kill(s.backend), RE_OK);
    re_dbg_session_free(&s);
}

static void test_detach_leaves_alive(void) {
    re_dbg_session_t s;
    re_dbg_event_t ev;
    re_dbg_session_init(&s);
    RE_CHECK_EQ_U(s.backend->vt->launch(s.backend, g_inferior, NULL), RE_OK);
    RE_CHECK_EQ_U(drain_create_break(s.backend, &ev), RE_OK);
    uint32_t pid = ev.pid;
    RE_CHECK_EQ_U(s.backend->vt->detach(s.backend), RE_OK);
    RE_CHECK(kill((pid_t)pid, 0) == 0);
    RE_CHECK(kill((pid_t)pid, SIGKILL) == 0);
    int st = 0;
    while (waitpid((pid_t)pid, &st, 0) < 0 && errno == EINTR) {
    }
    re_dbg_session_free(&s);
}

static void test_attach_detach(void) {
    pid_t child = fork();
    RE_CHECK(child >= 0);
    if (child == 0) {
        for (;;)
            pause();
        _exit(0);
    }
    re_dbg_session_t s;
    re_dbg_event_t ev;
    re_dbg_session_init(&s);
    /* Give the child a moment to enter pause. */
    usleep(50000);
    RE_CHECK_EQ_U(s.backend->vt->attach(s.backend, (uint32_t)child), RE_OK);
    RE_CHECK_EQ_U(drain_create_break(s.backend, &ev), RE_OK);
    RE_CHECK_EQ_U(ev.pid, (uint32_t)child);
    size_t n = 0;
    RE_CHECK_EQ_U(s.backend->vt->enum_threads(s.backend, NULL, 0, &n), RE_OK);
    RE_CHECK(n >= 1);
    RE_CHECK_EQ_U(s.backend->vt->detach(s.backend), RE_OK);
    RE_CHECK(kill(child, 0) == 0);
    RE_CHECK(kill(child, SIGKILL) == 0);
    int st = 0;
    while (waitpid(child, &st, 0) < 0 && errno == EINTR) {
    }
    re_dbg_session_free(&s);
}

#endif /* __linux__ */

int main(int argc, char **argv) {
#if defined(__linux__)
    g_inferior = getenv("RE_TEST_INFERIOR");
    if (!g_inferior && argc > 1)
        g_inferior = argv[1];
#endif
    test_session_lifecycle();
#if defined(__linux__)
    if (!g_inferior) {
        printf("FAIL missing RE_TEST_INFERIOR / argv[1] path to loop_inferior\n");
        return 1;
    }
    test_launch_entry_step();
    test_detach_leaves_alive();
    test_attach_detach();
#else
    (void)argc;
    (void)argv;
    test_stubs();
#endif
    return re_test_report("debug");
}
