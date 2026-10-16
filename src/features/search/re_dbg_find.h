// re_dbg_find.h - pattern and value search over inferior memory.
// Module: feature (C11).
// Owns: find/search hit table and pattern parse helpers for CLI/MCP.
// Depends: re_dbg session read_mem. Fixed caps; no allocation outside arenas.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "features/debug/re_dbg.h"

#define RE_FIND_MAX_HITS 64
#define RE_FIND_PAT_MAX 64
#define RE_FIND_CHUNK 4096

typedef struct {
    uint64_t addr;
} re_dbg_find_hit_t;

typedef struct {
    uint8_t bytes[RE_FIND_PAT_MAX];
    uint8_t mask[RE_FIND_PAT_MAX]; /* 0xff = must match, 0 = wildcard nibble/byte */
    size_t n;
} re_dbg_find_pat_t;

// Parse hex (optional ?? wildcards), "quoted" ASCII, or u8:/u16:/u32:/u64:value.
re_err_code_t re_dbg_find_parse(const char *spec, re_dbg_find_pat_t *out);

// Search [start, start+len). When len==0, walk loaded modules.
re_err_code_t re_dbg_session_find(re_dbg_session_t *s, uint64_t start, uint64_t len,
                                  const re_dbg_find_pat_t *pat, re_dbg_find_hit_t *hits, size_t cap,
                                  size_t *n_out, bool *truncated);

#ifdef __cplusplus
}
#endif
