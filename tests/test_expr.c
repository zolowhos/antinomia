// test_expr.c - unit tests for expression eval, $vars, watches, and BP cond fields.
// Module: test (C11).
// Owns: offline expression coverage without a live inferior.
// Depends: re_dbg, re_expr, re_test.
#include "features/debug/re_dbg.h"
#include "features/debug/re_dbg_bp.h"
#include "features/expr/re_expr.h"
#include "re_test.h"

#include <stdio.h>
#include <string.h>

int re_test_count = 0;
int re_test_fail = 0;

static void test_arith(void) {
    re_dbg_session_t s;
    int64_t v = 0;
    char err[RE_EXPR_ERR_MAX];
    re_dbg_session_init(&s);
    RE_CHECK_EQ_U(re_expr_eval(&s, "1+2*3", &v, err, sizeof(err)), RE_OK);
    RE_CHECK(v == 7);
    RE_CHECK_EQ_U(re_expr_eval(&s, "(1+2)*3", &v, err, sizeof(err)), RE_OK);
    RE_CHECK(v == 9);
    RE_CHECK_EQ_U(re_expr_eval(&s, "0x10 + 2", &v, err, sizeof(err)), RE_OK);
    RE_CHECK(v == 18);
    RE_CHECK_EQ_U(re_expr_eval(&s, "8 >> 2", &v, err, sizeof(err)), RE_OK);
    RE_CHECK(v == 2);
    RE_CHECK_EQ_U(re_expr_eval(&s, "1 && 0 || 1", &v, err, sizeof(err)), RE_OK);
    RE_CHECK(v == 1);
    RE_CHECK_EQ_U(re_expr_eval(&s, "!0", &v, err, sizeof(err)), RE_OK);
    RE_CHECK(v == 1);
    RE_CHECK_EQ_U(re_expr_eval(&s, "~0", &v, err, sizeof(err)), RE_OK);
    RE_CHECK(v == (int64_t)-1);
    re_dbg_session_free(&s);
}

static void test_vars(void) {
    re_dbg_session_t s;
    int64_t v = 0;
    char err[RE_EXPR_ERR_MAX];
    re_dbg_session_init(&s);
    RE_CHECK_EQ_U(re_dbg_session_var_set(&s, "x", 42), RE_OK);
    RE_CHECK_EQ_U(re_expr_eval(&s, "$x + 8", &v, err, sizeof(err)), RE_OK);
    RE_CHECK(v == 50);
    RE_CHECK_EQ_U(re_dbg_session_var_get(&s, "x", &v), RE_OK);
    RE_CHECK(v == 42);
    re_dbg_session_free(&s);
}

static void test_watch(void) {
    re_dbg_session_t s;
    re_dbg_watch_t list[8];
    size_t n = 0;
    int64_t v = 0;
    char err[96];
    re_dbg_session_init(&s);
    RE_CHECK_EQ_U(re_dbg_session_var_set(&s, "a", 3), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_watch_add(&s, "$a * 2"), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_watch_add(&s, "10+1"), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_watch_list(&s, list, 8, &n), RE_OK);
    RE_CHECK(n == 2);
    RE_CHECK_EQ_U(re_dbg_session_watch_eval(&s, 0, &v, err, sizeof(err)), RE_OK);
    RE_CHECK(v == 6);
    RE_CHECK_EQ_U(re_dbg_session_watch_eval(&s, 1, &v, err, sizeof(err)), RE_OK);
    RE_CHECK(v == 11);
    RE_CHECK_EQ_U(re_dbg_session_watch_clear(&s, 0), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_watch_list(&s, list, 8, &n), RE_OK);
    RE_CHECK(n == 1);
    RE_CHECK_EQ_U(re_dbg_session_watch_clear(&s, (size_t)-1), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_watch_list(&s, list, 8, &n), RE_OK);
    RE_CHECK(n == 0);
    re_dbg_session_free(&s);
}

static void test_bp_fields(void) {
    re_dbg_bps_t bps;
    re_dbg_bp_info_t info[4];
    size_t n = 0;
    re_dbg_bp_slot_t *slot;
    memset(&bps, 0, sizeof(bps));
    /* Plant without backend by forging a used slot. */
    bps.slot[0].used = true;
    bps.slot[0].addr = 0x1000;
    bps.slot[0].enabled = true;
    RE_CHECK_EQ_U(re_dbg_bp_set_field(&bps, 0x1000, "break", "rax==0"), RE_OK);
    RE_CHECK_EQ_U(re_dbg_bp_set_field(&bps, 0x1000, "log", "hit"), RE_OK);
    RE_CHECK_EQ_U(re_dbg_bp_set_field(&bps, 0x1000, "logcond", "1"), RE_OK);
    RE_CHECK_EQ_U(re_dbg_bp_set_field(&bps, 0x1000, "cmd", "regs"), RE_OK);
    RE_CHECK_EQ_U(re_dbg_bp_set_field(&bps, 0x1000, "cmdcond", "1"), RE_OK);
    RE_CHECK_EQ_U(re_dbg_bp_set_field(&bps, 0x1000, "fast", "1"), RE_OK);
    RE_CHECK_EQ_U(re_dbg_bp_list(&bps, info, 4, &n), RE_OK);
    RE_CHECK(n == 1);
    RE_CHECK(strcmp(info[0].break_cond, "rax==0") == 0);
    RE_CHECK(strcmp(info[0].log_text, "hit") == 0);
    RE_CHECK(info[0].fast_resume);
    slot = re_dbg_bp_find(&bps, 0x1000);
    RE_CHECK(slot != NULL);
    RE_CHECK(slot->fast_resume);
}

static void test_hitcount_var(void) {
    re_dbg_session_t s;
    int64_t v = 0;
    re_dbg_session_init(&s);
    s.eval_hit_count = 7;
    RE_CHECK_EQ_U(re_dbg_session_var_get(&s, "hitcount", &v), RE_OK);
    RE_CHECK(v == 7);
    RE_CHECK_EQ_U(re_dbg_session_eval(&s, "$hitcount + 1", &v), RE_OK);
    RE_CHECK(v == 8);
    re_dbg_session_free(&s);
}

static void test_cond_on_break_fast(void) {
    re_dbg_session_t s;
    re_dbg_event_t ev;
    re_dbg_bp_slot_t *slot;
    re_dbg_session_init(&s);
    RE_CHECK(s.bps != NULL);
    slot = &s.bps->slot[0];
    slot->used = true;
    slot->addr = 0x2000;
    slot->enabled = true;
    slot->fast_resume = true;
    slot->break_cond[0] = '0';
    slot->break_cond[1] = 0;
    memset(&ev, 0, sizeof(ev));
    ev.kind = RE_DBG_EV_BREAK;
    ev.addr = 0x2000;
    /* break_cond false + fast_resume swallows the event (prepare_cont no-ops). */
    RE_CHECK(re_dbg_session_cond_on_break(&s, &ev) == false);
    RE_CHECK(ev.kind == RE_DBG_EV_NONE);
    RE_CHECK(slot->hit_count == 1);
    slot->break_cond[0] = '1';
    slot->break_cond[1] = 0;
    ev.kind = RE_DBG_EV_BREAK;
    RE_CHECK(re_dbg_session_cond_on_break(&s, &ev) == true);
    RE_CHECK(slot->hit_count == 2);
    re_dbg_session_free(&s);
}

int main(void) {
    test_arith();
    test_vars();
    test_watch();
    test_bp_fields();
    test_hitcount_var();
    test_cond_on_break_fast();
    return re_test_report("expr");
}
