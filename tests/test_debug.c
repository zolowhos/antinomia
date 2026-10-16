// test_debug.c - session lifetime and backend stub wiring checks.
// Module: test (C11).
// Owns: assertions that session != command arena and stubs degrade cleanly.
// Depends: re_dbg, re_test. Prints to stdout, which is fine for a test binary.
#include "features/debug/re_dbg.h"
#include "re_test.h"

#include <string.h>

int re_test_count = 0;
int re_test_fail = 0;

static void test_stubs(void) {
    re_dbg_session_t s;
    re_dbg_event_t ev;
    re_dbg_regs_t regs;
    size_t n = 0;
    re_dbg_session_init(&s);
    RE_CHECK(s.backend != NULL);
    RE_CHECK(s.backend->vt != NULL);
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
    re_dbg_session_reset(&s);
    RE_CHECK(s.backend != NULL);
    re_dbg_session_free(&s);
}

static void test_session_api_degrades(void) {
    re_dbg_session_t s;
    re_dbg_event_t ev;
    re_arena_t cmd;
    void *scratch;
    re_dbg_session_init(&s);
    re_arena_init(&cmd, 4096);
    scratch = re_arena_alloc(&cmd, 64);
    RE_CHECK(scratch != NULL);
    RE_CHECK_EQ_U(re_dbg_session_launch(&s, "/bin/true", NULL), RE_E_UNSUPPORTED);
    RE_CHECK(!s.attached);
    RE_CHECK_EQ_U(re_dbg_session_attach(&s, 1), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(re_dbg_session_detach(&s), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(re_dbg_session_kill(&s), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(re_dbg_session_cont(&s), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(re_dbg_session_wait_event(&s, &ev, 0), RE_E_UNSUPPORTED);
    RE_CHECK_EQ_U(s.last_event.kind, RE_DBG_EV_NONE);
    // Command arena reset must not touch the session arena or backend.
    re_arena_reset(&cmd);
    RE_CHECK(s.backend != NULL);
    RE_CHECK(s.backend->vt != NULL);
    RE_CHECK(strcmp(re_dbg_event_kind_str(RE_DBG_EV_BREAK), "break") == 0);
    RE_CHECK(re_dbg_backend_name()[0] != 0);
    re_arena_free(&cmd);
    re_dbg_session_free(&s);
}

int main(void) {
    test_stubs();
    test_session_api_degrades();
    return re_test_report("debug");
}
