// test_ai.c - unit tests for local AI helpers (explain / exsum / suggest catalog).
// Module: test (C11).
// Owns: offline checks for re_ai_*; no live inferior required.
// Depends: re_ai, re_disasm, re_test. Prints to stdout (test binary OK).
#include "features/ai/re_ai.h"
#include "features/disasm/re_disasm.h"
#include "re_test.h"

#include <string.h>

int re_test_count = 0;
int re_test_fail = 0;

static void test_explain_call(void) {
    re_disasm_insn_t in;
    re_ai_explain_t ex;
    memset(&in, 0, sizeof(in));
    in.addr = 0x401000;
    in.size = 5;
    in.flags = RE_DISASM_F_CALL;
    in.target = 0x401200;
    memcpy(in.text, "call 0x401200", 14);
    RE_CHECK_EQ_U(re_ai_explain_insn(&in, &ex), RE_OK);
    RE_CHECK(strcmp(ex.kind, "call") == 0);
    RE_CHECK(ex.summary[0] != 0);
    RE_CHECK(ex.detail[0] != 0);
}

static void test_explain_ret(void) {
    re_disasm_insn_t in;
    re_ai_explain_t ex;
    memset(&in, 0, sizeof(in));
    in.addr = 0x100;
    in.size = 1;
    in.flags = RE_DISASM_F_RET;
    memcpy(in.text, "ret", 4);
    RE_CHECK_EQ_U(re_ai_explain_insn(&in, &ex), RE_OK);
    RE_CHECK(strcmp(ex.kind, "ret") == 0);
}

static void test_exsum_sigsegv(void) {
    re_ai_exsum_t sum;
    re_dbg_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = RE_DBG_EV_EXCEPTION;
    ev.code = 11;
    ev.addr = 0xdeadbeef;
    ev.first_chance = true;
    RE_CHECK_EQ_U(re_ai_summarize_exception(&ev, 0, false, &sum), RE_OK);
    RE_CHECK(strcmp(sum.name, "SIGSEGV") == 0);
    RE_CHECK(sum.from_event);
    RE_CHECK(sum.first_chance);
    RE_CHECK_EQ_U(sum.addr, 0xdeadbeefull);
}

static void test_exsum_access_violation(void) {
    re_ai_exsum_t sum;
    RE_CHECK_EQ_U(re_ai_summarize_exception(NULL, (int32_t)0xC0000005, true, &sum), RE_OK);
    RE_CHECK(strcmp(sum.name, "ACCESS_VIOLATION") == 0);
    RE_CHECK(!sum.from_event);
    RE_CHECK(sum.advice[0] != 0);
}

static void test_exsum_needs_input(void) {
    re_ai_exsum_t sum;
    re_dbg_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = RE_DBG_EV_BREAKPOINT;
    RE_CHECK_EQ_U(re_ai_summarize_exception(&ev, 0, false, &sum), RE_E_USAGE);
    RE_CHECK_EQ_U(re_ai_summarize_exception(NULL, 0, false, &sum), RE_E_USAGE);
}

static void test_suggest_requires_attach(void) {
    re_dbg_session_t s;
    re_ai_bp_site_t sites[4];
    size_t n = 99;
    re_dbg_session_init(&s);
    s.attached = false;
    RE_CHECK_EQ_U(re_ai_suggest_bp(&s, sites, 4, &n), RE_E_USAGE);
    re_dbg_session_free(&s);
}

int main(void) {
    test_explain_call();
    test_explain_ret();
    test_exsum_sigsegv();
    test_exsum_access_violation();
    test_exsum_needs_input();
    test_suggest_requires_attach();
    return re_test_report("ai");
}
