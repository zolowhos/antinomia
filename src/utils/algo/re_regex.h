// re_regex.h - backtracking regex over bounded text, for binary string search.
// Module: util (C11).
// Owns: pattern compilation into a flat instruction program and the match engine.
// Depends: re_arena, re_err, re_str. No I/O, no globals, ASCII only.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

#include "utils/mem/re_arena.h"
#include "utils/sys/re_err.h"
#include "utils/text/re_str.h"

#define RE_RX_MAX_INST 512
#define RE_RX_MAX_STACK 2048
#define RE_RX_MAX_CLASS 8
#define RE_RX_MAX_REPEAT 64

// Opaque. The layout lives in re_regex_priv.h so this public header stays thin.
typedef struct re_rx re_rx_t;

// Compile a pattern. Returns NULL and fills err on a syntax error. flags
// currently understands "i" for case insensitive.
re_rx_t *re_rx_compile(re_arena_t *a, const char *pattern, const char *flags, re_err_t *err);

void re_rx_free(re_rx_t *rx);

// Scan the whole slice for the first match. Returns true and sets start and end
// to byte offsets into the slice, or false when nothing matches.
bool re_rx_search(const re_rx_t *rx, re_str_t text, size_t *start, size_t *end);

// Match anchored at one exact position, for filter style use.
bool re_rx_match_at(const re_rx_t *rx, re_str_t text, size_t at, size_t *end);

// True when the pattern is only literal text, which lets callers use a fast
// substring scan instead of the engine.
bool re_rx_is_literal(const re_rx_t *rx, const char **out, size_t *out_n);

#ifdef __cplusplus
}
#endif
