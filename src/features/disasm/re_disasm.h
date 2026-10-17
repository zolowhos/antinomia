// re_disasm.h - x86-64 decode helpers and a multi-slot instruction cache.
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
#define RE_DISASM_COMMENT_MAX 48
#define RE_DISASM_CACHE_N 64
#define RE_DISASM_CACHE_SLOTS 4
#define RE_DISASM_CACHE_BYTES 256

enum {
    RE_DISASM_F_CALL = 1u << 0,
    RE_DISASM_F_JMP = 1u << 1,
    RE_DISASM_F_JCC = 1u << 2,
    RE_DISASM_F_RET = 1u << 3,
    RE_DISASM_F_IMM = 1u << 4,
    RE_DISASM_F_RIPREL = 1u << 5
};

typedef struct {
    uint64_t addr;
    uint8_t size;
    uint8_t bytes[15];
    char text[RE_DISASM_TEXT_MAX];
    char comment[RE_DISASM_COMMENT_MAX];
    uint32_t flags;
    uint64_t target; /* absolute branch/call target when flags say so */
    uint64_t mem;    /* rip-rel or absolute immediate pointer candidate */
} re_disasm_insn_t;

typedef struct {
    bool valid;
    uint64_t base;
    size_t span;
    size_t n;
    uint32_t used; /* LRU stamp */
    uint8_t raw[RE_DISASM_CACHE_BYTES];
    re_disasm_insn_t lines[RE_DISASM_CACHE_N];
} re_disasm_slot_t;

typedef struct {
    re_disasm_slot_t slots[RE_DISASM_CACHE_SLOTS];
    uint32_t clock;
    uint32_t hits;
    uint32_t misses;
    uint32_t invalidations;
    /* Mirror of the most recently filled/hit slot for existing callers. */
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

// Fill flags/target/mem from raw bytes (native Capstone stand-in for x86-64).
void re_disasm_enrich(re_disasm_insn_t *insn);

void re_disasm_cache_clear(re_disasm_cache_t *c);
void re_disasm_cache_invalidate(re_disasm_cache_t *c, uint64_t addr, size_t n);

// Fill or hit a slot from a memory window. Matching base/span/bytes is a hit.
re_err_code_t re_disasm_cache_fill(re_disasm_cache_t *c, uint64_t addr, const uint8_t *buf,
                                   size_t n, size_t max_n);

void re_disasm_cache_stats(const re_disasm_cache_t *c, uint32_t *hits, uint32_t *misses,
                           uint32_t *invalidations);

#ifdef __cplusplus
}
#endif
