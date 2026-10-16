// test_debug.c - session lifetime, Linux process-control, Windows inactive checks.
// Module: test (C11).
// Owns: session arena isolation, live Linux control, Windows inactive checks.
// Depends: re_dbg, re_test. Prints to stdout, which is fine for a test binary.
#if defined(__linux__)
#    define _GNU_SOURCE
#endif
#include "features/debug/re_dbg.h"
#include "re_test.h"

#include <stdio.h>
#include <string.h>

#if defined(__linux__)
#    include <errno.h>
#    include <signal.h>
#    include <stdlib.h>
#    include <unistd.h>
#    include <sys/types.h>
#    include <sys/wait.h>
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

static void test_session_arena_isolation(void) {
    re_dbg_session_t s;
    re_arena_t cmd;
    void *scratch;
    re_dbg_session_init(&s);
    re_arena_init(&cmd, 4096);
    scratch = re_arena_alloc(&cmd, 64);
    RE_CHECK(scratch != NULL);
    RE_CHECK(strcmp(re_dbg_event_kind_str(RE_DBG_EV_BREAK), "break") == 0);
    RE_CHECK(re_dbg_backend_name()[0] != 0);
    // Command arena reset must not touch the session arena or backend.
    re_arena_reset(&cmd);
    RE_CHECK(s.backend != NULL);
    RE_CHECK(s.backend->vt != NULL);
    re_arena_free(&cmd);
    re_dbg_session_free(&s);
}

#if defined(_WIN32)

static void test_win_inactive(void) {
    re_dbg_session_t s;
    re_dbg_event_t ev;
    re_dbg_regs_t regs;
    size_t n = 0;
    re_dbg_session_init(&s);
    RE_CHECK(s.backend != NULL);
    RE_CHECK(s.backend->vt != NULL);
    RE_CHECK(s.backend->impl != NULL);
    RE_CHECK_EQ_U(s.backend->vt->detach(s.backend), RE_E_USAGE);
    RE_CHECK_EQ_U(s.backend->vt->kill(s.backend), RE_E_USAGE);
    RE_CHECK_EQ_U(s.backend->vt->wait_event(s.backend, &ev, 0), RE_E_USAGE);
    RE_CHECK_EQ_U(s.backend->vt->cont(s.backend), RE_E_USAGE);
    RE_CHECK_EQ_U(s.backend->vt->launch(s.backend, "", NULL), RE_E_USAGE);
    RE_CHECK_EQ_U(s.backend->vt->launch(s.backend, "Z:\\no-such-antinomia-target.exe", NULL),
                  RE_E_IO);
    RE_CHECK_EQ_U(s.backend->vt->attach(s.backend, 0), RE_E_USAGE);
    RE_CHECK_EQ_U(s.backend->vt->step(s.backend), RE_E_USAGE);
    RE_CHECK_EQ_U(s.backend->vt->pause(s.backend), RE_E_USAGE);
    RE_CHECK_EQ_U(s.backend->vt->get_regs(s.backend, 0, &regs), RE_E_USAGE);
    RE_CHECK_EQ_U(s.backend->vt->set_regs(s.backend, 0, &regs), RE_E_USAGE);
    RE_CHECK_EQ_U(s.backend->vt->read_mem(s.backend, 0, NULL, 0), RE_E_USAGE);
    RE_CHECK_EQ_U(s.backend->vt->write_mem(s.backend, 0, NULL, 0), RE_E_USAGE);
    RE_CHECK_EQ_U(s.backend->vt->enum_modules(s.backend, NULL, 0, &n), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(re_dbg_session_detach(&s), RE_E_USAGE);
    RE_CHECK_EQ_U(re_dbg_session_kill(&s), RE_E_USAGE);
    re_dbg_session_free(&s);
}

#elif defined(__linux__)

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

static void test_session_api_live(void) {
    re_dbg_session_t s;
    re_dbg_event_t ev;
    re_dbg_session_init(&s);
    RE_CHECK(g_inferior != NULL);
    RE_CHECK_EQ_U(re_dbg_session_launch(&s, g_inferior, NULL), RE_OK);
    RE_CHECK(s.attached);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_CREATE);
    RE_CHECK_EQ_U(s.last_event.kind, RE_DBG_EV_CREATE);
    RE_CHECK(s.pid != 0);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_BREAK);
    RE_CHECK_EQ_U(re_dbg_session_kill(&s), RE_OK);
    RE_CHECK(!s.attached);
    re_dbg_session_free(&s);
}

static void test_regs_mem_disasm(void) {
    re_dbg_session_t s;
    re_dbg_event_t ev;
    re_dbg_regs_t regs;
    re_disasm_insn_t lines[8];
    size_t n = 0;
    uint8_t buf[16];
    uint8_t patch[1] = {0x90};
    uint8_t saved;
    re_dbg_session_init(&s);
    RE_CHECK(g_inferior != NULL);
    RE_CHECK_EQ_U(re_dbg_session_launch(&s, g_inferior, NULL), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_CREATE);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_BREAK);
    memset(&regs, 0, sizeof(regs));
    RE_CHECK_EQ_U(re_dbg_session_get_regs(&s, 0, &regs), RE_OK);
    RE_CHECK(regs.rip != 0);
    RE_CHECK(regs.rsp != 0);
    regs.rax = 0x1122334455667788ull;
    RE_CHECK_EQ_U(re_dbg_session_set_regs(&s, 0, &regs), RE_OK);
    memset(&regs, 0, sizeof(regs));
    RE_CHECK_EQ_U(re_dbg_session_get_regs(&s, 0, &regs), RE_OK);
    RE_CHECK(regs.rax == 0x1122334455667788ull);
    memset(buf, 0, sizeof(buf));
    RE_CHECK_EQ_U(re_dbg_session_read_mem(&s, regs.rip, buf, sizeof(buf)), RE_OK);
    saved = buf[0];
    RE_CHECK_EQ_U(re_dbg_session_disasm(&s, regs.rip, 4, lines, &n), RE_OK);
    RE_CHECK(n >= 1);
    RE_CHECK(s.disasm_cache.valid);
    RE_CHECK_EQ_U(re_dbg_session_write_mem(&s, regs.rip, patch, 1), RE_OK);
    RE_CHECK(!s.disasm_cache.valid);
    RE_CHECK_EQ_U(re_dbg_session_read_mem(&s, regs.rip, buf, 1), RE_OK);
    RE_CHECK(buf[0] == 0x90);
    patch[0] = saved;
    RE_CHECK_EQ_U(re_dbg_session_write_mem(&s, regs.rip, patch, 1), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_disasm(&s, regs.rip, 2, lines, &n), RE_OK);
    RE_CHECK(s.disasm_cache.valid);
    RE_CHECK_EQ_U(re_dbg_session_kill(&s), RE_OK);
    re_dbg_session_free(&s);
}

static void test_soft_bp_step_pause(void) {
    re_dbg_session_t s;
    re_dbg_event_t ev;
    re_dbg_regs_t regs;
    uint8_t byte = 0;
    uint8_t orig = 0;
    uint64_t bp_addr;
    re_dbg_bp_info_t info[8];
    size_t n = 0;
    re_dbg_session_init(&s);
    RE_CHECK_EQ_U(re_dbg_session_launch(&s, g_inferior, NULL), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_CREATE);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_BREAK);
    RE_CHECK_EQ_U(s.backend->vt->get_regs(s.backend, 0, &regs), RE_OK);
    bp_addr = regs.rip;
    RE_CHECK_EQ_U(s.backend->vt->read_mem(s.backend, bp_addr, &orig, 1), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_bp_set(&s, bp_addr), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_bp_list(&s, info, 8, &n), RE_OK);
    RE_CHECK_EQ_U(n, 1);
    RE_CHECK(info[0].enabled);
    RE_CHECK_EQ_U(s.backend->vt->read_mem(s.backend, bp_addr, &byte, 1), RE_OK);
    RE_CHECK_EQ_U(byte, 0xcc);
    RE_CHECK_EQ_U(re_dbg_session_bp_disable(&s, bp_addr), RE_OK);
    RE_CHECK_EQ_U(s.backend->vt->read_mem(s.backend, bp_addr, &byte, 1), RE_OK);
    RE_CHECK_EQ_U(byte, orig);
    RE_CHECK_EQ_U(re_dbg_session_bp_enable(&s, bp_addr), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_cont(&s), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_BREAK);
    RE_CHECK_EQ_U(ev.addr, bp_addr);
    RE_CHECK_EQ_U(s.backend->vt->read_mem(s.backend, bp_addr, &byte, 1), RE_OK);
    RE_CHECK_EQ_U(byte, orig);
    RE_CHECK_EQ_U(re_dbg_session_step(&s), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_STEP);
    RE_CHECK_EQ_U(re_dbg_session_bp_clear(&s, bp_addr), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_cont(&s), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_pause(&s), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK(ev.kind == RE_DBG_EV_BREAK || ev.kind == RE_DBG_EV_EXCEPTION);
    RE_CHECK_EQ_U(re_dbg_session_kill(&s), RE_OK);
    re_dbg_session_free(&s);
}


#else /* non-Linux, non-Windows host */

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

static void test_session_api_degrades(void) {
    re_dbg_session_t s;
    re_dbg_event_t ev;
    re_dbg_session_init(&s);
    RE_CHECK_EQ_U(re_dbg_session_launch(&s, "/bin/true", NULL), RE_E_UNSUPPORTED);
    RE_CHECK(!s.attached);
    RE_CHECK_EQ_U(re_dbg_session_attach(&s, 1), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(re_dbg_session_detach(&s), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(re_dbg_session_kill(&s), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(re_dbg_session_cont(&s), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 0), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(s.last_event.kind, RE_DBG_EV_NONE);
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
    test_session_lifecycle();
    test_session_arena_isolation();
#if defined(_WIN32)
    test_win_inactive();
#elif defined(__linux__)
    if (!g_inferior) {
        printf("FAIL missing RE_TEST_INFERIOR / argv[1] path to loop_inferior\n");
        return 1;
    }
    test_launch_entry_step();
    test_detach_leaves_alive();
    test_attach_detach();
    test_session_api_live();
    test_regs_mem_disasm();
    test_soft_bp_step_pause();
#else
    test_stubs();
    test_session_api_degrades();
#endif
    return re_test_report("debug");
}
