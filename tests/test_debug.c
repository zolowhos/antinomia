// test_debug.c - backend vtable and session lifetime checks for Phase 1.
// Module: test (C11).
// Owns: assertions that backends link and inactive ops fail cleanly.
// Depends: re_dbg, re_test. Prints to stdout, which is fine for a test binary.
#include "features/debug/re_dbg.h"
#include "re_test.h"

#include <string.h>

int re_test_count = 0;
int re_test_fail = 0;

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
    re_dbg_session_reset(&s);
    RE_CHECK(s.backend != NULL);
    re_dbg_session_free(&s);
}
#else
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
#endif

int main(void) {
#if defined(_WIN32)
    test_win_inactive();
#else
    test_stubs();
#endif
    return re_test_report("debug");
}
