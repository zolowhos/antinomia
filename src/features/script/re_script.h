// re_script.h - brace format, notes/log, and trace paging helpers.
// Module: feature/script (C11).
// Owns: {rax}-style format, session notes/log append, trace list paging.
// Depends: re_dbg session, re_expr for brace bodies.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "features/debug/re_dbg.h"
#include "utils/sys/re_err.h"

/* Expand {expr} braces using the session expression engine (regs/$vars). */
re_err_code_t re_script_format(re_dbg_session_t *s, const char *fmt, char *out, size_t cap);

re_err_code_t re_dbg_session_note_add(re_dbg_session_t *s, const char *text);
re_err_code_t re_dbg_session_note_list(re_dbg_session_t *s, const char **out, size_t cap,
                                       size_t *n_out);
re_err_code_t re_dbg_session_log_add(re_dbg_session_t *s, const char *text);
re_err_code_t re_dbg_session_log_list(re_dbg_session_t *s, const char **out, size_t cap,
                                      size_t *n_out);
re_err_code_t re_dbg_session_traces_list(re_dbg_session_t *s, re_dbg_trace_rec_t *out, size_t cap,
                                        size_t *n_out);
re_err_code_t re_dbg_session_traces_push(re_dbg_session_t *s, const re_dbg_trace_rec_t *rec);
re_err_code_t re_dbg_session_traces_clear(re_dbg_session_t *s);

/* Copy and clear the pending on-BP command string. */
bool re_dbg_session_consume_pending_cmd(re_dbg_session_t *s, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
