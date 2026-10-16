// re_disasm.h - x86-64 decode helpers and a small instruction cache.
// Module: feature (C11).
// Owns: one-instruction decode, multi-line walk, and write-side invalidation.
// Depends: re_err. Capstone is optional later; this native path is always linked.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "utils/sys/re_err.h"

#define RE_DISASM_TEXT_MAX 64
#define RE_DISASM_CACHE_N 64

typedef struct {
    uint64_t addr;
    uint8_t size;
    uint8_t bytes[15];
    char text[RE_DISASM_TEXT_MAX];
} re_disasm_insn_t;

typedef struct {
    bool valid;
    uint64_t base;
    size_t span;
    size_t n;
    re_disasm_insn_t lines[RE_DISASM_CACHE_N];
} re_disasm_cache_t;

// Decode one instruction. Returns byte length (1..15) or 0 when buf is empty.
size_t re_disasm_x64_one(uint64_t addr, const uint8_t *buf, size_t n, re_disasm_insn_t *out);

// Decode up to max_n instructions into out. Writes count to n_out.
re_err_code_t re_disasm_x64_many(uint64_t addr, const uint8_t *buf, size_t n, size_t max_n,
                                 re_disasm_insn_t *out, size_t *n_out);

void re_disasm_cache_clear(re_disasm_cache_t *c);
void re_disasm_cache_invalidate(re_disasm_cache_t *c, uint64_t addr, size_t n);

// Fill cache from a memory window. On hit with matching base/span, copies cache.
re_err_code_t re_disasm_cache_fill(re_disasm_cache_t *c, uint64_t addr, const uint8_t *buf,
                                   size_t n, size_t max_n);

#ifdef __cplusplus
}
#endif
