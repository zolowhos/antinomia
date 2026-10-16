// re_dbg_advbp.c - hardware, memory, and module-load breakpoint helpers.
// Module: feature (C11).
// Owns: DR-slot table, mem BP via HW watch, bpdll filters, rendezvous arm.
// Depends: re_dbg_advbp.h. Fidelity limits: docs/bp-fidelity.md.
#include "features/bp/re_dbg_advbp.h"

#include "utils/text/re_str.h"

#include <string.h>

static re_err_code_t note(re_dbg_session_t *s, re_err_code_t c, const char *msg) {
    if (!s)
        return c == RE_OK ? RE_E_INTERNAL : c;
    if (c == RE_OK)
        RE_ERR_OK(&s->last_err);
    else
        RE_ERR_SET(&s->last_err, c, msg);
    return c;
}

static re_err_code_t need_attached(re_dbg_session_t *s) {
    if (!s || !s->backend || !s->backend->vt)
        return note(s, RE_E_INTERNAL, "debug session has no backend");
    if (!s->attached)
        return note(s, RE_E_USAGE, "not attached");
    return RE_OK;
}

static bool size_ok(uint8_t size) {
    return size == 1u || size == 2u || size == 4u || size == 8u;
}

static int alloc_hw_slot(re_dbg_advbp_t *adv) {
    int i;
    for (i = 0; i < RE_DBG_MAX_HW; i++) {
        if (!adv->hw[i].used)
            return i;
    }
    return -1;
}

static re_dbg_hw_slot_t *find_hw(re_dbg_advbp_t *adv, uint64_t addr) {
    int i;
    for (i = 0; i < RE_DBG_MAX_HW; i++) {
        if (adv->hw[i].used && adv->hw[i].addr == addr)
            return &adv->hw[i];
    }
    return NULL;
}

static re_dbg_mem_slot_t *find_mem(re_dbg_advbp_t *adv, uint64_t addr) {
    size_t i;
    for (i = 0; i < RE_DBG_MAX_MEM_BP; i++) {
        if (adv->mem[i].used && adv->mem[i].addr == addr)
            return &adv->mem[i];
    }
    return NULL;
}

void re_dbg_advbp_clear(re_dbg_advbp_t *adv) {
    if (!adv)
        return;
    memset(adv, 0, sizeof(*adv));
}

re_err_code_t re_dbg_advbp_disarm_all(re_dbg_advbp_t *adv, re_dbg_backend_t *b) {
    int i;
    if (!adv || !b || !b->vt)
        return RE_E_INTERNAL;
    if (b->vt->hw_bp_clear) {
        for (i = 0; i < RE_DBG_MAX_HW; i++) {
            if (adv->hw[i].used && adv->hw[i].enabled)
                (void)b->vt->hw_bp_clear(b, adv->hw[i].slot);
        }
    }
    adv->rendezvous_armed = false;
    adv->rendezvous_addr = 0;
    return RE_OK;
}

const char *re_dbg_bp_kind_str(re_dbg_bp_kind_t kind) {
    switch (kind) {
        case RE_DBG_BP_KIND_SOFT:
            return "soft";
        case RE_DBG_BP_KIND_HW:
            return "hw";
        case RE_DBG_BP_KIND_MEM:
            return "mem";
        case RE_DBG_BP_KIND_LOAD:
            return "load";
        default:
            return "unknown";
    }
}

const char *re_dbg_bp_access_str(re_dbg_bp_access_t access) {
    switch (access) {
        case RE_DBG_BP_AX_X:
            return "x";
        case RE_DBG_BP_AX_W:
            return "w";
        case RE_DBG_BP_AX_RW:
            return "rw";
        default:
            return "?";
    }
}

static re_err_code_t plant_hw(re_dbg_session_t *s, re_dbg_hw_slot_t *hs) {
    if (!s->backend->vt->hw_bp_set)
        return note(s, RE_E_UNSUPPORTED, "hw_bp_set is not available");
    return s->backend->vt->hw_bp_set(s->backend, hs->slot, hs->addr, hs->size, hs->access);
}

re_err_code_t re_dbg_session_hw_bp_set(re_dbg_session_t *s, uint64_t addr, uint8_t size,
                                       re_dbg_bp_access_t access) {
    re_dbg_hw_slot_t *hs;
    re_err_code_t c;
    int slot;
    c = need_attached(s);
    if (c != RE_OK)
        return c;
    if (!s->advbp)
        return note(s, RE_E_INTERNAL, "advbp table missing");
    if (!size_ok(size))
        return note(s, RE_E_USAGE, "hw bp size must be 1, 2, 4, or 8");
    if (access != RE_DBG_BP_AX_X && access != RE_DBG_BP_AX_W && access != RE_DBG_BP_AX_RW)
        return note(s, RE_E_USAGE, "hw bp access must be x, w, or rw");
    hs = find_hw(s->advbp, addr);
    if (!hs) {
        slot = alloc_hw_slot(s->advbp);
        if (slot < 0)
            return note(s, RE_E_RANGE, "no free hardware breakpoint slots");
        hs = &s->advbp->hw[slot];
        memset(hs, 0, sizeof(*hs));
        hs->used = true;
        hs->slot = slot;
        hs->addr = addr;
    }
    hs->size = size;
    hs->access = (uint8_t)access;
    hs->enabled = true;
    c = plant_hw(s, hs);
    if (c != RE_OK)
        return note(s, c, "hw_bp_set failed");
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_hw_bp_clear(re_dbg_session_t *s, uint64_t addr) {
    re_dbg_hw_slot_t *hs;
    re_err_code_t c = need_attached(s);
    if (c != RE_OK)
        return c;
    hs = find_hw(s->advbp, addr);
    if (!hs)
        return note(s, RE_E_NOTFOUND, "hardware breakpoint not found");
    if (s->backend->vt->hw_bp_clear)
        (void)s->backend->vt->hw_bp_clear(s->backend, hs->slot);
    memset(hs, 0, sizeof(*hs));
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_mem_bp_set(re_dbg_session_t *s, uint64_t addr, size_t len,
                                        re_dbg_bp_access_t access) {
    re_dbg_mem_slot_t *ms;
    re_dbg_caps_t caps;
    re_err_code_t c;
    uint8_t size;
    size_t i;
    c = need_attached(s);
    if (c != RE_OK)
        return c;
    if (!s->advbp)
        return note(s, RE_E_INTERNAL, "advbp table missing");
    caps = re_dbg_session_caps(s);
    if ((caps.bits & RE_DBG_CAP_MEM_BP) == 0)
        return note(s, RE_E_UNSUPPORTED, "memory breakpoints not supported");
    if (access == RE_DBG_BP_AX_X)
        return note(s, RE_E_USAGE, "mem bp is watch-only; use bph for execute");
    if (!size_ok((uint8_t)len))
        return note(s, RE_E_USAGE, "mem bp len must be 1, 2, 4, or 8 (see docs/bp-fidelity.md)");
    size = (uint8_t)len;
    ms = find_mem(s->advbp, addr);
    if (!ms) {
        for (i = 0; i < RE_DBG_MAX_MEM_BP; i++) {
            if (!s->advbp->mem[i].used) {
                ms = &s->advbp->mem[i];
                break;
            }
        }
        if (!ms)
            return note(s, RE_E_RANGE, "no free memory breakpoint slots");
        memset(ms, 0, sizeof(*ms));
        ms->used = true;
        ms->addr = addr;
        ms->hw_slot = -1;
    }
    ms->len = len;
    ms->access = (uint8_t)access;
    ms->enabled = true;
    c = re_dbg_session_hw_bp_set(s, addr, size, access);
    if (c != RE_OK)
        return c;
    {
        re_dbg_hw_slot_t *hs = find_hw(s->advbp, addr);
        ms->hw_slot = hs ? hs->slot : -1;
    }
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_mem_bp_clear(re_dbg_session_t *s, uint64_t addr) {
    re_dbg_mem_slot_t *ms;
    re_err_code_t c = need_attached(s);
    if (c != RE_OK)
        return c;
    ms = find_mem(s->advbp, addr);
    if (!ms)
        return note(s, RE_E_NOTFOUND, "memory breakpoint not found");
    (void)re_dbg_session_hw_bp_clear(s, addr);
    memset(ms, 0, sizeof(*ms));
    return note(s, RE_OK, NULL);
}

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

re_err_code_t re_dbg_session_load_bp_set(re_dbg_session_t *s, const char *name) {
    re_dbg_load_slot_t *ls = NULL;
    size_t i;
    re_err_code_t c = need_attached(s);
    if (c != RE_OK)
        return c;
    if (!name || !name[0])
        return note(s, RE_E_USAGE, "bpdll needs a module name substring");
    if (!s->advbp || !s->bps)
        return note(s, RE_E_INTERNAL, "breakpoint tables missing");
    for (i = 0; i < RE_DBG_MAX_LOAD_BP; i++) {
        if (s->advbp->load[i].used && re_str_eq_cstr(re_str(s->advbp->load[i].name), name)) {
            ls = &s->advbp->load[i];
            break;
        }
        if (!ls && !s->advbp->load[i].used)
            ls = &s->advbp->load[i];
    }
    if (!ls)
        return note(s, RE_E_RANGE, "no free module-load breakpoint slots");
    memset(ls, 0, sizeof(*ls));
    ls->used = true;
    ls->enabled = true;
    copy_name(ls->name, sizeof(ls->name), name);
    c = re_dbg_advbp_arm_rendezvous(s->advbp, s->backend, s->bps);
    if (c != RE_OK && c != RE_E_UNSUPPORTED)
        return note(s, c, "failed to arm module-load rendezvous");
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_load_bp_clear(re_dbg_session_t *s, const char *name) {
    size_t i;
    bool any = false;
    re_err_code_t c = need_attached(s);
    if (c != RE_OK)
        return c;
    if (!name || !name[0])
        return note(s, RE_E_USAGE, "bpdll clear needs a name");
    for (i = 0; i < RE_DBG_MAX_LOAD_BP; i++) {
        if (!s->advbp->load[i].used)
            continue;
        if (!re_str_eq_cstr(re_str(s->advbp->load[i].name), name))
            continue;
        memset(&s->advbp->load[i], 0, sizeof(s->advbp->load[i]));
        any = true;
    }
    if (!any)
        return note(s, RE_E_NOTFOUND, "module-load breakpoint not found");
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_advbp_arm_rendezvous(re_dbg_advbp_t *adv, re_dbg_backend_t *b,
                                          re_dbg_bps_t *soft) {
    uint64_t addr = 0;
    re_err_code_t c;
    if (!adv || !b || !b->vt || !soft)
        return RE_E_INTERNAL;
    if (!b->vt->rendezvous_addr)
        return RE_E_UNSUPPORTED;
    c = b->vt->rendezvous_addr(b, &addr);
    if (c != RE_OK)
        return c;
    if (addr == 0)
        return RE_OK; /* Windows: LOAD_DLL path; nothing to plant */
    c = re_dbg_bp_set(soft, b, addr, false);
    if (c != RE_OK)
        return c;
    adv->rendezvous_addr = addr;
    adv->rendezvous_armed = true;
    return RE_OK;
}

bool re_dbg_advbp_load_matches(const re_dbg_advbp_t *adv, const char *path) {
    size_t i;
    if (!adv || !path)
        return false;
    for (i = 0; i < RE_DBG_MAX_LOAD_BP; i++) {
        if (!adv->load[i].used || !adv->load[i].enabled)
            continue;
        if (re_str_find_cstr(re_str(path), adv->load[i].name, 0) >= 0)
            return true;
    }
    return false;
}

bool re_dbg_advbp_has_load(const re_dbg_advbp_t *adv) {
    size_t i;
    if (!adv)
        return false;
    for (i = 0; i < RE_DBG_MAX_LOAD_BP; i++) {
        if (adv->load[i].used && adv->load[i].enabled)
            return true;
    }
    return false;
}

bool re_dbg_advbp_is_rendezvous(const re_dbg_advbp_t *adv, uint64_t addr) {
    return adv && adv->rendezvous_armed && addr != 0 && addr == adv->rendezvous_addr;
}

bool re_dbg_advbp_on_break(re_dbg_advbp_t *adv, re_dbg_backend_t *b, uint32_t tid,
                           uint64_t *addr_inout) {
    int slot = -1;
    uint64_t hit = 0;
    if (!adv || !b || !b->vt || !addr_inout)
        return false;
    if (adv->rendezvous_armed && *addr_inout == adv->rendezvous_addr)
        return true;
    if (!b->vt->hw_bp_hit)
        return false;
    if (b->vt->hw_bp_hit(b, tid, &slot, &hit) != RE_OK || slot < 0)
        return false;
    *addr_inout = hit;
    return true;
}

static void fill_soft(re_dbg_bp_ex_t *dst, const re_dbg_bp_info_t *src) {
    memset(dst, 0, sizeof(*dst));
    dst->addr = src->addr;
    dst->orig = src->orig;
    dst->enabled = src->enabled;
    dst->temporary = src->temporary;
    dst->kind = RE_DBG_BP_KIND_SOFT;
    dst->fast_resume = src->fast_resume;
    dst->hit_count = src->hit_count;
    copy_name(dst->break_cond, sizeof(dst->break_cond), src->break_cond);
    copy_name(dst->log_text, sizeof(dst->log_text), src->log_text);
    copy_name(dst->log_cond, sizeof(dst->log_cond), src->log_cond);
    copy_name(dst->cmd, sizeof(dst->cmd), src->cmd);
    copy_name(dst->cmd_cond, sizeof(dst->cmd_cond), src->cmd_cond);
}

static size_t append_hw_mem_load(re_dbg_advbp_t *adv, re_dbg_bp_ex_t *out, size_t cap, size_t n) {
    size_t i;
    for (i = 0; i < RE_DBG_MAX_HW; i++) {
        if (!adv->hw[i].used || find_mem(adv, adv->hw[i].addr))
            continue;
        if (out && n < cap) {
            memset(&out[n], 0, sizeof(out[n]));
            out[n].addr = adv->hw[i].addr;
            out[n].enabled = adv->hw[i].enabled;
            out[n].kind = RE_DBG_BP_KIND_HW;
            out[n].access = adv->hw[i].access;
            out[n].size = adv->hw[i].size;
        }
        n++;
    }
    for (i = 0; i < RE_DBG_MAX_MEM_BP; i++) {
        if (!adv->mem[i].used)
            continue;
        if (out && n < cap) {
            memset(&out[n], 0, sizeof(out[n]));
            out[n].addr = adv->mem[i].addr;
            out[n].enabled = adv->mem[i].enabled;
            out[n].kind = RE_DBG_BP_KIND_MEM;
            out[n].access = adv->mem[i].access;
            out[n].len = adv->mem[i].len;
            out[n].size = (uint8_t)adv->mem[i].len;
        }
        n++;
    }
    for (i = 0; i < RE_DBG_MAX_LOAD_BP; i++) {
        if (!adv->load[i].used)
            continue;
        if (out && n < cap) {
            memset(&out[n], 0, sizeof(out[n]));
            out[n].enabled = adv->load[i].enabled;
            out[n].kind = RE_DBG_BP_KIND_LOAD;
            copy_name(out[n].name, sizeof(out[n].name), adv->load[i].name);
        }
        n++;
    }
    return n;
}

re_err_code_t re_dbg_session_bp_list_ex(re_dbg_session_t *s, re_dbg_bp_ex_t *out, size_t cap,
                                        size_t *n_out) {
    re_dbg_bp_info_t soft[RE_DBG_MAX_BP];
    size_t nsoft = 0, n = 0, i;
    re_err_code_t c;
    if (n_out)
        *n_out = 0;
    if (!s || !s->bps || !s->advbp)
        return note(s, RE_E_INTERNAL, "breakpoint tables missing");
    c = re_dbg_bp_list(s->bps, soft, RE_DBG_MAX_BP, &nsoft);
    if (c != RE_OK)
        return note(s, c, "bp list failed");
    for (i = 0; i < nsoft; i++) {
        if (out && n < cap)
            fill_soft(&out[n], &soft[i]);
        n++;
    }
    n = append_hw_mem_load(s->advbp, out, cap, n);
    if (n_out)
        *n_out = n;
    return note(s, RE_OK, NULL);
}
