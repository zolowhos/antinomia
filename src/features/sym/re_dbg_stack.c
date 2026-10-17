// re_dbg_stack.c - stack walk v0: frame-pointer chain or simple RSP dump.
// Module: feature/sym (C11).
// Owns: re_dbg_session_bt and best-effort module/symbol annotate per frame.
// Depends: re_dbg.h, re_dbg_os. Uses backend get_regs and read_mem only.
#include "features/debug/re_dbg.h"
#include "features/os/re_dbg_os.h"

#include "utils/sys/re_path.h"

#include <string.h>

#define RE_DBG_BT_MAX 64
#define RE_DBG_BT_RSP_WORDS 32

static void copy_name(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    if (!dst || cap == 0)
        return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static bool addr_in_mod(const re_dbg_module_t *m, uint64_t addr) {
    return m && addr >= m->base && addr < m->base + (m->size ? m->size : 1);
}

static void annotate_frame(re_dbg_session_t *s, re_dbg_frame_t *fr, const re_dbg_module_t *mods,
                           size_t n_mod) {
    size_t i;
    fr->module[0] = 0;
    fr->symbol[0] = 0;
    for (i = 0; i < n_mod; i++) {
        char sym[128];
        uint64_t sym_addr = 0;
        if (!addr_in_mod(&mods[i], fr->ip))
            continue;
        copy_name(fr->module, sizeof(fr->module), re_path_basename_ptr(mods[i].path));
        if (s &&
            re_dbg_sym_nearest_file(&s->arena, mods[i].path, mods[i].base, fr->ip, sym, sizeof(sym),
                                    &sym_addr) == RE_OK &&
            sym[0]) {
            if (sym_addr == fr->ip)
                copy_name(fr->symbol, sizeof(fr->symbol), sym);
            else {
                /* Keep offset so rich bt shows module+symbol+delta. */
                size_t n = 0;
                while (sym[n] && n + 1 < sizeof(fr->symbol)) {
                    fr->symbol[n] = sym[n];
                    n++;
                }
                fr->symbol[n] = 0;
            }
        }
        return;
    }
}

static bool looks_like_fp(uint64_t fp, uint64_t sp) {
    if (fp == 0 || (fp & 7u) != 0)
        return false;
    if (sp && fp < sp)
        return false;
    return true;
}

static re_err_code_t walk_fp(re_dbg_backend_t *b, uint64_t rip, uint64_t rsp, uint64_t rbp,
                             re_dbg_frame_t *out, size_t cap, size_t *n_out) {
    size_t n = 0;
    uint64_t fp = rbp;
    uint64_t ip = rip;
    uint64_t sp = rsp;
    size_t guard = 0;
    if (cap == 0 || !out) {
        if (n_out)
            *n_out = 0;
        return RE_OK;
    }
    while (n < cap && guard++ < RE_DBG_BT_MAX) {
        uint64_t next[2];
        memset(&out[n], 0, sizeof(out[n]));
        out[n].ip = ip;
        out[n].sp = sp;
        out[n].fp = fp;
        n++;
        if (!looks_like_fp(fp, sp))
            break;
        if (b->vt->read_mem(b, fp, next, sizeof(next)) != RE_OK)
            break;
        if (next[0] == 0 || next[0] == fp)
            break;
        sp = fp + 16;
        ip = next[1];
        fp = next[0];
        if (ip == 0)
            break;
    }
    if (n_out)
        *n_out = n;
    return RE_OK;
}

static re_err_code_t dump_rsp(re_dbg_backend_t *b, uint64_t rip, uint64_t rsp, re_dbg_frame_t *out,
                              size_t cap, size_t *n_out) {
    uint64_t words[RE_DBG_BT_RSP_WORDS];
    size_t n = 0;
    size_t got = 0;
    if (cap == 0 || !out) {
        if (n_out)
            *n_out = 0;
        return RE_OK;
    }
    memset(&out[0], 0, sizeof(out[0]));
    out[0].ip = rip;
    out[0].sp = rsp;
    n = 1;
    if (rsp && b->vt->read_mem(b, rsp, words, sizeof(words)) == RE_OK)
        got = RE_DBG_BT_RSP_WORDS;
    for (size_t i = 0; i < got && n < cap; i++) {
        uint64_t cand = words[i];
        if (cand < 0x1000 || (cand & 0xfull) == 0xfull)
            continue;
        /* Heuristic: keep canonical user-space looking pointers. */
        if (cand > 0x00007fffffffffffULL)
            continue;
        memset(&out[n], 0, sizeof(out[n]));
        out[n].ip = cand;
        out[n].sp = rsp + (uint64_t)i * 8u;
        n++;
    }
    if (n_out)
        *n_out = n;
    return RE_OK;
}

re_err_code_t re_dbg_session_enum_modules(re_dbg_session_t *s, re_dbg_module_t *out, size_t cap,
                                          size_t *n_out) {
    re_err_code_t c;
    size_t n = 0;
    if (n_out)
        *n_out = 0;
    if (!s || !s->backend || !s->backend->vt || !s->backend->vt->enum_modules)
        return RE_E_INTERNAL;
    if (!s->attached)
        return RE_E_USAGE;
    c = s->backend->vt->enum_modules(s->backend, out, cap, &n);
    if (c != RE_OK)
        return c;
    if (out && n)
        (void)re_dbg_os_enrich_modules(s, out, n < cap ? n : cap);
    if (n_out)
        *n_out = n;
    return RE_OK;
}

re_err_code_t re_dbg_session_bt(re_dbg_session_t *s, re_dbg_frame_t *out, size_t cap,
                                size_t *n_out) {
    re_dbg_regs_t regs;
    static re_dbg_module_t mods[RE_DBG_MAX_MODULES];
    size_t n_mod = 0, n = 0, i;
    re_err_code_t c;
    if (n_out)
        *n_out = 0;
    if (!s || !s->backend || !s->backend->vt)
        return RE_E_INTERNAL;
    if (!s->attached)
        return RE_E_USAGE;
    if (!s->backend->vt->get_regs || !s->backend->vt->read_mem)
        return RE_E_UNSUPPORTED;
    memset(&regs, 0, sizeof(regs));
    c = s->backend->vt->get_regs(s->backend, s->tid, &regs);
    if (c != RE_OK)
        return c;
    if (looks_like_fp(regs.rbp, regs.rsp))
        c = walk_fp(s->backend, regs.rip, regs.rsp, regs.rbp, out, cap, &n);
    else
        c = dump_rsp(s->backend, regs.rip, regs.rsp, out, cap, &n);
    if (c != RE_OK)
        return c;
    (void)re_dbg_session_enum_modules(s, mods, RE_DBG_MAX_MODULES, &n_mod);
    if (out) {
        for (i = 0; i < n; i++)
            annotate_frame(s, &out[i], mods, n_mod);
    }
    if (n_out)
        *n_out = n;
    return RE_OK;
}
