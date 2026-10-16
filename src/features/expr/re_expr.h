// re_expr.h - C-like debugger expression evaluator for BP/watch/CLI.
// Module: feature/expr (C11).
// Owns: expression grammar API and typed memory dereference helpers.
// Depends: re_dbg session for regs, mem, symbols, and $vars. Arena optional.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "features/debug/re_dbg.h"
#include "utils/sys/re_err.h"

#define RE_EXPR_ERR_MAX 96

// Evaluate a C-like expression against the live session.
// Supports: regs, $vars, [mem], byte/word/dword/qword:[mem], module:symbol,
// arithmetic/bitwise/logical ops, and parentheses. Result is signed 64-bit.
re_err_code_t re_expr_eval(re_dbg_session_t *s, const char *src, int64_t *out, char *err,
                           size_t err_cap);

// Parse-only address helper: same grammar, returns unsigned address bits.
re_err_code_t re_expr_eval_u64(re_dbg_session_t *s, const char *src, uint64_t *out, char *err,
                               size_t err_cap);

#ifdef __cplusplus
}
#endif
