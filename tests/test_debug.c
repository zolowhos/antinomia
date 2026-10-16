// test_debug.c - session lifetime, JSON DB, Linux integ, Windows inactive checks.
// Module: test (C11).
// Owns: session arena isolation, module DB, live Linux control and integ path.
// Depends: re_dbg, re_dbg_db, re_dbg_bp, re_test. Prints to stdout (test binary).
#if defined(__linux__)
#    define _GNU_SOURCE
#endif
#include "features/debug/re_dbg.h"
#include "features/debug/re_dbg_bp.h"
#include "features/debug/re_dbg_db.h"
#include "re_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#    include <process.h>
#    define re_test_pid() ((int)_getpid())
#else
#    include <unistd.h>
#    define re_test_pid() ((int)getpid())
#endif

#if defined(__linux__)
#    include <errno.h>
#    include <signal.h>
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

static void test_module_db_roundtrip(void) {
    re_dbg_db_t db;
    re_arena_t a;
    char path[512];
    char dbpath[RE_DBG_DB_PATH_MAX];
    const char *tmpdir = getenv("TMPDIR");
#if defined(_WIN32)
    if (!tmpdir || !tmpdir[0])
        tmpdir = getenv("TEMP");
    if (!tmpdir || !tmpdir[0])
        tmpdir = ".";
#else
    if (!tmpdir || !tmpdir[0])
        tmpdir = "/tmp";
#endif
    snprintf(path, sizeof(path), "%s/antinomia-db-mod-%d", tmpdir, re_test_pid());
    RE_CHECK_EQ_U(re_dbg_db_path_for(path, dbpath, sizeof(dbpath)), RE_OK);
    remove(dbpath);
    re_arena_init(&a, 64u * 1024u);
    RE_CHECK_EQ_U(re_dbg_db_load(&db, &a, path), RE_OK);
    RE_CHECK_EQ_U(re_dbg_db_add_bp(&db, 0x1000u, true), RE_OK);
    RE_CHECK_EQ_U(re_dbg_db_add_label(&db, 0x1000u, "entry"), RE_OK);
    RE_CHECK_EQ_U(re_dbg_db_add_comment(&db, 0x1004u, "hook"), RE_OK);
    RE_CHECK_EQ_U(re_dbg_db_save(&db, &a), RE_OK);
    re_dbg_db_clear(&db);
    RE_CHECK_EQ_U(re_dbg_db_load(&db, &a, path), RE_OK);
    RE_CHECK_EQ_U(db.n_bps, 1u);
    RE_CHECK_EQ_HEX(db.bps[0].rva, 0x1000u);
    RE_CHECK(db.bps[0].enabled);
    RE_CHECK_EQ_U(db.n_labels, 1u);
    RE_CHECK_EQ_STR(db.labels[0].name, "entry");
    RE_CHECK_EQ_U(db.n_comments, 1u);
    RE_CHECK_EQ_STR(db.comments[0].text, "hook");
    re_arena_free(&a);
    remove(dbpath);
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
    RE_CHECK_EQ_U(s.backend->vt->step(s.backend), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(s.backend->vt->get_regs(s.backend, 0, &regs), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(s.backend->vt->set_regs(s.backend, 0, &regs), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(s.backend->vt->read_mem(s.backend, 0, NULL, 0), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(s.backend->vt->write_mem(s.backend, 0, NULL, 0), RE_E_UNSUPPORTED);
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

/* Headless CLI/MCP surface: session API only, no TTY. */
static void test_launch_bp_regs_step_detach(void) {
    re_dbg_session_t s;
    re_dbg_event_t ev;
    re_dbg_regs_t regs;
    re_dbg_swbp_t bp;
    uint8_t byte = 0;
    uint32_t pid = 0;
    uint64_t rip = 0;
    re_dbg_session_init(&s);
    RE_CHECK(g_inferior != NULL);
    RE_CHECK_EQ_U(re_dbg_session_launch(&s, g_inferior, NULL), RE_OK);
    RE_CHECK(s.db.loaded);
    RE_CHECK_EQ_U(re_dbg_db_add_label(&s.db, 0x0u, "entry_bp"), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_CREATE);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_BREAK);
    pid = s.pid;
    RE_CHECK(pid != 0);
    RE_CHECK_EQ_U(re_dbg_session_get_regs(&s, &regs), RE_OK);
    RE_CHECK(regs.rip != 0);
    RE_CHECK_EQ_U(re_dbg_session_step(&s), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 2000), RE_OK);
    RE_CHECK_EQ_U(ev.kind, RE_DBG_EV_STEP);
    RE_CHECK_EQ_U(re_dbg_session_get_regs(&s, &regs), RE_OK);
    rip = regs.rip;
    RE_CHECK(rip != 0);
    RE_CHECK_EQ_U(s.backend->vt->read_mem(s.backend, rip, &byte, 1), RE_OK);
    RE_CHECK_EQ_U(re_dbg_swbp_set(s.backend, rip, &bp), RE_OK);
    RE_CHECK(bp.active);
    RE_CHECK_EQ_U(bp.orig, byte);
    RE_CHECK_EQ_U(re_dbg_swbp_clear(s.backend, &bp), RE_OK);
    RE_CHECK(!bp.active);
    RE_CHECK_EQ_U(re_dbg_session_detach(&s), RE_OK);
    RE_CHECK(!s.attached);
    if (s.db.db_path[0])
        remove(s.db.db_path);
    RE_CHECK(kill((pid_t)pid, 0) == 0);
    RE_CHECK(kill((pid_t)pid, SIGKILL) == 0);
    {
        int st = 0;
        while (waitpid((pid_t)pid, &st, 0) < 0 && errno == EINTR) {
        }
    }
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
    test_module_db_roundtrip();
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
    test_launch_bp_regs_step_detach();
#else
    test_stubs();
    test_session_api_degrades();
#endif
    return re_test_report("debug");
}
