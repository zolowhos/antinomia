// re_dbg_bp.c - software breakpoint plant, restore, and re-arm.
// Module: feature (C11).
// Owns: int3 table ops used by session cont/step and wait_event post-process.
// Depends: re_dbg_bp.h. Uses backend read/write_mem and get/set_regs only.
#include "features/debug/re_dbg_bp.h"

#include <string.h>

static re_dbg_bp_slot_t *find_slot(re_dbg_bps_t *bps, uint64_t addr) {
    size_t i;
    if (!bps)
        return NULL;
    for (i = 0; i < RE_DBG_MAX_BP; i++) {
        if (bps->slot[i].used && bps->slot[i].addr == addr)
            return &bps->slot[i];
    }
    return NULL;
}

static re_dbg_bp_slot_t *alloc_slot(re_dbg_bps_t *bps) {
    size_t i;
    for (i = 0; i < RE_DBG_MAX_BP; i++) {
        if (!bps->slot[i].used)
            return &bps->slot[i];
    }
    return NULL;
}

static re_err_code_t plant(re_dbg_backend_t *b, re_dbg_bp_slot_t *s) {
    static const uint8_t cc = 0xcc;
    re_err_code_t c;
    if (!b || !b->vt || !b->vt->write_mem || !s)
        return RE_E_INTERNAL;
    c = b->vt->write_mem(b, s->addr, &cc, 1);
    if (c != RE_OK)
        return c;
    s->armed = true;
    return RE_OK;
}

static re_err_code_t restore(re_dbg_backend_t *b, re_dbg_bp_slot_t *s) {
    re_err_code_t c;
    if (!b || !b->vt || !b->vt->write_mem || !s)
        return RE_E_INTERNAL;
    if (!s->armed)
        return RE_OK;
    c = b->vt->write_mem(b, s->addr, &s->orig, 1);
    if (c != RE_OK)
        return c;
    s->armed = false;
    return RE_OK;
}

void re_dbg_bps_clear(re_dbg_bps_t *bps) {
    if (!bps)
        return;
    memset(bps, 0, sizeof(*bps));
}

re_err_code_t re_dbg_bp_set(re_dbg_bps_t *bps, re_dbg_backend_t *b, uint64_t addr, bool temporary) {
    re_dbg_bp_slot_t *s;
    uint8_t orig = 0;
    re_err_code_t c;
    if (!bps || !b || !b->vt || !b->vt->read_mem)
        return RE_E_INTERNAL;
    s = find_slot(bps, addr);
    if (s) {
        s->temporary = temporary;
        s->enabled = true;
        if (!s->armed)
            return plant(b, s);
        return RE_OK;
    }
    s = alloc_slot(bps);
    if (!s)
        return RE_E_RANGE;
    c = b->vt->read_mem(b, addr, &orig, 1);
    if (c != RE_OK)
        return c;
    memset(s, 0, sizeof(*s));
    s->used = true;
    s->addr = addr;
    s->orig = orig;
    s->enabled = true;
    s->temporary = temporary;
    return plant(b, s);
}

re_err_code_t re_dbg_bp_clear(re_dbg_bps_t *bps, re_dbg_backend_t *b, uint64_t addr) {
    re_dbg_bp_slot_t *s = find_slot(bps, addr);
    re_err_code_t c;
    if (!s)
        return RE_E_NOTFOUND;
    c = restore(b, s);
    if (c != RE_OK)
        return c;
    if (bps->need_rearm && bps->rearm_addr == addr)
        bps->need_rearm = false;
    memset(s, 0, sizeof(*s));
    return RE_OK;
}

re_err_code_t re_dbg_bp_enable(re_dbg_bps_t *bps, re_dbg_backend_t *b, uint64_t addr) {
    re_dbg_bp_slot_t *s = find_slot(bps, addr);
    if (!s)
        return RE_E_NOTFOUND;
    s->enabled = true;
    if (!s->armed)
        return plant(b, s);
    return RE_OK;
}

re_err_code_t re_dbg_bp_disable(re_dbg_bps_t *bps, re_dbg_backend_t *b, uint64_t addr) {
    re_dbg_bp_slot_t *s = find_slot(bps, addr);
    if (!s)
        return RE_E_NOTFOUND;
    s->enabled = false;
    return restore(b, s);
}

re_err_code_t re_dbg_bp_list(const re_dbg_bps_t *bps, re_dbg_bp_info_t *out, size_t cap,
                             size_t *n_out) {
    size_t i, n = 0;
    if (n_out)
        *n_out = 0;
    if (!bps)
        return RE_E_INTERNAL;
    for (i = 0; i < RE_DBG_MAX_BP; i++) {
        if (!bps->slot[i].used)
            continue;
        if (out && n < cap) {
            out[n].addr = bps->slot[i].addr;
            out[n].orig = bps->slot[i].orig;
            out[n].enabled = bps->slot[i].enabled;
            out[n].temporary = bps->slot[i].temporary;
        }
        n++;
    }
    if (n_out)
        *n_out = n;
    return RE_OK;
}

static re_err_code_t rewind_rip(re_dbg_backend_t *b, uint32_t tid, uint64_t addr) {
    re_dbg_regs_t regs;
    re_err_code_t c;
    if (!b->vt->get_regs || !b->vt->set_regs)
        return RE_E_UNSUPPORTED;
    c = b->vt->get_regs(b, tid, &regs);
    if (c != RE_OK)
        return c;
    regs.rip = addr;
    return b->vt->set_regs(b, tid, &regs);
}

bool re_dbg_bp_on_hit(re_dbg_bps_t *bps, re_dbg_backend_t *b, uint32_t tid, uint64_t *addr_inout) {
    re_dbg_bp_slot_t *s;
    uint64_t hit;
    if (!bps || !b || !addr_inout)
        return false;
    hit = *addr_inout;
    s = find_slot(bps, hit);
    if (!s)
        s = find_slot(bps, hit > 0 ? hit - 1u : 0);
    if (!s || !s->enabled)
        return false;
    hit = s->addr;
    *addr_inout = hit;
    if (restore(b, s) != RE_OK)
        return false;
    if (rewind_rip(b, tid, hit) != RE_OK)
        return false;
    bps->need_rearm = s->enabled && !s->temporary;
    bps->rearm_addr = hit;
    if (s->temporary) {
        memset(s, 0, sizeof(*s));
        bps->step_over_active = false;
        bps->need_rearm = false;
    }
    return true;
}

re_err_code_t re_dbg_bp_prepare_cont(re_dbg_bps_t *bps, re_dbg_backend_t *b) {
    if (!bps || !b || !b->vt || !b->vt->step)
        return RE_E_INTERNAL;
    if (!bps->need_rearm)
        return RE_OK;
    bps->rearm_then_cont = true;
    return b->vt->step(b);
}

re_err_code_t re_dbg_bp_after_step(re_dbg_bps_t *bps, re_dbg_backend_t *b, bool *consumed) {
    re_dbg_bp_slot_t *s;
    re_err_code_t c;
    if (consumed)
        *consumed = false;
    if (!bps || !b)
        return RE_E_INTERNAL;
    if (!bps->need_rearm)
        return RE_OK;
    s = find_slot(bps, bps->rearm_addr);
    bps->need_rearm = false;
    if (s && s->enabled) {
        c = plant(b, s);
        if (c != RE_OK)
            return c;
    }
    if (bps->rearm_then_cont) {
        bps->rearm_then_cont = false;
        if (consumed)
            *consumed = true;
        if (!b->vt->cont)
            return RE_E_UNSUPPORTED;
        return b->vt->cont(b);
    }
    return RE_OK;
}

re_err_code_t re_dbg_bp_disarm_all(re_dbg_bps_t *bps, re_dbg_backend_t *b) {
    size_t i;
    re_err_code_t c = RE_OK;
    if (!bps)
        return RE_E_INTERNAL;
    for (i = 0; i < RE_DBG_MAX_BP; i++) {
        if (!bps->slot[i].used || !bps->slot[i].armed)
            continue;
        c = restore(b, &bps->slot[i]);
        if (c != RE_OK)
            return c;
    }
    bps->need_rearm = false;
    bps->rearm_then_cont = false;
    return RE_OK;
}
