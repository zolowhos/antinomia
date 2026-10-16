// re_disasm_cfg.h - function-local control-flow graph from basic blocks.
// Module: feature (C11).
// Owns: block/edge build over a Capstone-class native decode window.
// Depends: re_disasm, re_dbg. Arena-free fixed caps for CLI/MCP JSON.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "features/debug/re_dbg.h"
#include "features/disasm/re_disasm.h"

#define RE_CFG_MAX_BLOCKS 48
#define RE_CFG_MAX_EDGES 96
#define RE_CFG_MAX_INSNS 128
#define RE_CFG_WINDOW 2048

typedef struct {
    uint64_t start;
    uint64_t end;
    size_t insn_count;
    bool is_ret;
    bool is_call;
} re_cfg_block_t;

typedef struct {
    uint64_t from;
    uint64_t to;
    char kind[12]; /* fall / taken / call */
} re_cfg_edge_t;

typedef struct {
    uint64_t entry;
    re_cfg_block_t blocks[RE_CFG_MAX_BLOCKS];
    size_t n_blocks;
    re_cfg_edge_t edges[RE_CFG_MAX_EDGES];
    size_t n_edges;
    bool truncated;
} re_cfg_t;

// Build a function-local CFG starting at entry (default RIP when 0).
re_err_code_t re_dbg_session_cfg(re_dbg_session_t *s, uint64_t entry, re_cfg_t *out);

#ifdef __cplusplus
}
#endif
