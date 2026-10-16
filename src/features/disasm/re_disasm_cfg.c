// re_disasm_cfg.c - function-local CFG via native decode + basic blocks.
// Module: feature (C11).
// Owns: leader discovery, block split, and fall/taken/call edges.
// Depends: re_disasm_cfg.h. Capstone optional later; x64 native path always runs.
#include "features/disasm/re_disasm_cfg.h"

#include <string.h>

static void copy_kind(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    if (!dst || cap == 0)
        return;
    while (src[i] && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static bool add_leader(uint64_t *leaders, size_t *n, uint64_t addr) {
    size_t i;
    if (*n >= RE_CFG_MAX_INSNS)
        return false;
    for (i = 0; i < *n; i++) {
        if (leaders[i] == addr)
            return true;
    }
    leaders[(*n)++] = addr;
    return true;
}

static void sort_leaders(uint64_t *a, size_t n) {
    size_t i, j;
    for (i = 1; i < n; i++) {
        uint64_t v = a[i];
        j = i;
        while (j > 0 && a[j - 1] > v) {
            a[j] = a[j - 1];
            j--;
        }
        a[j] = v;
    }
}

static int find_insn(const re_disasm_insn_t *ins, size_t n, uint64_t addr) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (ins[i].addr == addr)
            return (int)i;
    }
    return -1;
}

static bool push_edge(re_cfg_t *g, uint64_t from, uint64_t to, const char *kind) {
    if (g->n_edges >= RE_CFG_MAX_EDGES) {
        g->truncated = true;
        return false;
    }
    g->edges[g->n_edges].from = from;
    g->edges[g->n_edges].to = to;
    copy_kind(g->edges[g->n_edges].kind, sizeof(g->edges[0].kind), kind);
    g->n_edges++;
    return true;
}

static void build_blocks(re_cfg_t *g, const re_disasm_insn_t *ins, size_t nins,
                         const uint64_t *leaders, size_t nlead) {
    size_t li;
    for (li = 0; li < nlead && g->n_blocks < RE_CFG_MAX_BLOCKS; li++) {
        int idx = find_insn(ins, nins, leaders[li]);
        re_cfg_block_t *b;
        size_t count = 0;
        if (idx < 0)
            continue;
        b = &g->blocks[g->n_blocks++];
        memset(b, 0, sizeof(*b));
        b->start = leaders[li];
        b->end = b->start;
        while ((size_t)idx + count < nins) {
            const re_disasm_insn_t *in = &ins[(size_t)idx + count];
            uint64_t next_addr = in->addr + in->size;
            b->end = next_addr;
            count++;
            if (in->flags & RE_DISASM_F_RET)
                b->is_ret = true;
            if (in->flags & RE_DISASM_F_CALL)
                b->is_call = true;
            if (in->flags & (RE_DISASM_F_JMP | RE_DISASM_F_JCC | RE_DISASM_F_RET))
                break;
            if (li + 1u < nlead && next_addr == leaders[li + 1u])
                break;
        }
        b->insn_count = count;
    }
}

static void build_edges(re_cfg_t *g, const re_disasm_insn_t *ins, size_t nins) {
    size_t bi;
    for (bi = 0; bi < g->n_blocks; bi++) {
        re_cfg_block_t *b = &g->blocks[bi];
        int idx = find_insn(ins, nins, b->start);
        const re_disasm_insn_t *last;
        if (idx < 0 || b->insn_count == 0)
            continue;
        last = &ins[(size_t)idx + b->insn_count - 1u];
        if (last->flags & RE_DISASM_F_RET)
            continue;
        if (last->flags & RE_DISASM_F_JMP) {
            if (last->target)
                (void)push_edge(g, b->start, last->target, "taken");
            continue;
        }
        if (last->flags & RE_DISASM_F_JCC) {
            if (last->target)
                (void)push_edge(g, b->start, last->target, "taken");
            (void)push_edge(g, b->start, last->addr + last->size, "fall");
            continue;
        }
        if (last->flags & RE_DISASM_F_CALL) {
            if (last->target)
                (void)push_edge(g, b->start, last->target, "call");
            (void)push_edge(g, b->start, last->addr + last->size, "fall");
            continue;
        }
        (void)push_edge(g, b->start, last->addr + last->size, "fall");
    }
}

re_err_code_t re_dbg_session_cfg(re_dbg_session_t *s, uint64_t entry, re_cfg_t *out) {
    uint8_t win[RE_CFG_WINDOW];
    re_disasm_insn_t ins[RE_CFG_MAX_INSNS];
    uint64_t leaders[RE_CFG_MAX_INSNS];
    size_t nins = 0, nlead = 0, i;
    re_dbg_regs_t regs;
    re_err_code_t c;
    if (!s || !out)
        return RE_E_USAGE;
    memset(out, 0, sizeof(*out));
    if (!s->attached)
        return RE_E_USAGE;
    if (entry == 0) {
        memset(&regs, 0, sizeof(regs));
        c = re_dbg_session_get_regs(s, 0, &regs);
        if (c != RE_OK)
            return c;
        entry = regs.rip;
    }
    out->entry = entry;
    memset(win, 0, sizeof(win));
    c = re_dbg_session_read_mem(s, entry, win, sizeof(win));
    if (c != RE_OK)
        return c;
    c = re_disasm_x64_many(entry, win, sizeof(win), RE_CFG_MAX_INSNS, ins, &nins);
    if (c != RE_OK || nins == 0)
        return c != RE_OK ? c : RE_E_USAGE;
    for (i = 0; i < nins; i++) {
        re_disasm_enrich(&ins[i]);
        if (ins[i].flags & RE_DISASM_F_RET && i + 1u < nins)
            out->truncated = (i + 8u < nins);
    }
    (void)add_leader(leaders, &nlead, entry);
    for (i = 0; i < nins; i++) {
        uint64_t fall = ins[i].addr + ins[i].size;
        if (ins[i].flags & (RE_DISASM_F_JCC | RE_DISASM_F_CALL))
            (void)add_leader(leaders, &nlead, fall);
        if (ins[i].flags & (RE_DISASM_F_JMP | RE_DISASM_F_JCC | RE_DISASM_F_CALL)) {
            if (ins[i].target >= entry && ins[i].target < entry + sizeof(win))
                (void)add_leader(leaders, &nlead, ins[i].target);
        }
        if (ins[i].flags & RE_DISASM_F_RET)
            break;
    }
    sort_leaders(leaders, nlead);
    build_blocks(out, ins, nins, leaders, nlead);
    build_edges(out, ins, nins);
    if (nins >= RE_CFG_MAX_INSNS)
        out->truncated = true;
    return RE_OK;
}
