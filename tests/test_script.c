// test_script.c - unit tests for brace format, notes/log, traces, pending cmd.
// Module: test (C11).
// Owns: offline Pack I coverage without a live inferior.
// Depends: re_dbg, re_script, re_test.
#include "features/debug/re_dbg.h"
#include "features/debug/re_dbg_bp.h"
#include "features/script/re_script.h"
#include "re_test.h"

#include <stdio.h>
#include <string.h>

int re_test_count = 0;
int re_test_fail = 0;

static void test_format_vars(void) {
    re_dbg_session_t s;
    char out[128];
    re_dbg_session_init(&s);
    RE_CHECK_EQ_U(re_dbg_session_var_set(&s, "x", 0x2a), RE_OK);
    RE_CHECK_EQ_U(re_script_format(&s, "hit {$x}", out, sizeof(out)), RE_OK);
    RE_CHECK(strcmp(out, "hit 0x2a") == 0);
    RE_CHECK_EQ_U(re_script_format(&s, "a={1+2} b", out, sizeof(out)), RE_OK);
    RE_CHECK(strcmp(out, "a=0x3 b") == 0);
    /* Unknown brace body stays literal when eval fails. */
    RE_CHECK_EQ_U(re_script_format(&s, "r={rax}", out, sizeof(out)), RE_OK);
    RE_CHECK(strcmp(out, "r={rax}") == 0);
    re_dbg_session_free(&s);
}

static void test_notes_log(void) {
    re_dbg_session_t s;
    const char *rows[32];
    size_t n = 0;
    re_dbg_session_init(&s);
    RE_CHECK_EQ_U(re_dbg_session_var_set(&s, "n", 7), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_note_add(&s, "note {$n}"), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_note_list(&s, rows, 32, &n), RE_OK);
    RE_CHECK(n == 1);
    RE_CHECK(strcmp(rows[0], "note 0x7") == 0);
    RE_CHECK_EQ_U(re_dbg_session_log_add(&s, "log {$n}"), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_log_list(&s, rows, 32, &n), RE_OK);
    RE_CHECK(n == 1);
    RE_CHECK(strcmp(rows[0], "log 0x7") == 0);
    re_dbg_session_free(&s);
}

static void test_traces_page(void) {
    re_dbg_session_t s;
    re_dbg_trace_rec_t rec;
    re_dbg_trace_rec_t out[8];
    size_t n = 0;
    size_t i;
    re_dbg_session_init(&s);
    for (i = 0; i < 5; i++) {
        memset(&rec, 0, sizeof(rec));
        rec.rip = 0x1000 + i;
        rec.rax = i;
        RE_CHECK_EQ_U(re_dbg_session_trace_push(&s, &rec), RE_OK);
    }
    RE_CHECK_EQ_U(re_dbg_session_trace_list(&s, out, 8, &n), RE_OK);
    RE_CHECK(n == 5);
    RE_CHECK(out[0].rip == 0x1000);
    RE_CHECK(out[4].rax == 4);
    RE_CHECK_EQ_U(re_dbg_session_trace_clear(&s), RE_OK);
    RE_CHECK_EQ_U(re_dbg_session_trace_list(&s, out, 8, &n), RE_OK);
    RE_CHECK(n == 0);
    re_dbg_session_free(&s);
}

static void test_pending_consume(void) {
    re_dbg_session_t s;
    char cmd[96];
    re_dbg_session_init(&s);
    memcpy(s.pending_cmd, "regs", 5);
    RE_CHECK(re_dbg_session_consume_pending_cmd(&s, cmd, sizeof(cmd)));
    RE_CHECK(strcmp(cmd, "regs") == 0);
    RE_CHECK(s.pending_cmd[0] == 0);
    RE_CHECK(!re_dbg_session_consume_pending_cmd(&s, cmd, sizeof(cmd)));
    re_dbg_session_free(&s);
}

static void test_bp_log_formats(void) {
    re_dbg_session_t s;
    re_dbg_event_t ev;
    re_dbg_bp_slot_t *slot;
    const char *rows[16];
    size_t n = 0;
    re_dbg_session_init(&s);
    RE_CHECK_EQ_U(re_dbg_session_var_set(&s, "k", 9), RE_OK);
    RE_CHECK(s.bps != NULL);
    slot = &s.bps->slot[0];
    slot->used = true;
    slot->addr = 0x3000;
    slot->enabled = true;
    memcpy(slot->log_text, "bp {$k}", 8);
    memset(&ev, 0, sizeof(ev));
    ev.kind = RE_DBG_EV_BREAK;
    ev.addr = 0x3000;
    RE_CHECK(re_dbg_session_cond_on_break(&s, &ev) == true);
    RE_CHECK_EQ_U(re_dbg_session_log_list(&s, rows, 16, &n), RE_OK);
    RE_CHECK(n == 1);
    RE_CHECK(strcmp(rows[0], "bp 0x9") == 0);
    re_dbg_session_free(&s);
}

int main(void) {
    test_format_vars();
    test_notes_log();
    test_traces_page();
    test_pending_consume();
    test_bp_log_formats();
    return re_test_report("script");
}
