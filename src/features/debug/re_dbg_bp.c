// re_dbg_bp.c - software breakpoint plant, restore, and re-arm.
// Module: feature (C11).
// Owns: int3 table ops and x86 CALL length for step-over.
// Depends: re_dbg_bp.h. Uses backend read/write_mem and get/set_regs only.
#include "features/debug/re_dbg_bp.h"

#include "utils/text/re_str.h"

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

re_dbg_bp_slot_t *re_dbg_bp_find(re_dbg_bps_t *bps, uint64_t addr) {
    return find_slot(bps, addr);
}

static void copy_field(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    if (!dst || cap == 0)
        return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void fill_info(const re_dbg_bp_slot_t *s, re_dbg_bp_info_t *out) {
    out->addr = s->addr;
    out->orig = s->orig;
    out->enabled = s->enabled;
    out->temporary = s->temporary;
    out->fast_resume = s->fast_resume;
    out->hit_count = s->hit_count;
    copy_field(out->break_cond, sizeof(out->break_cond), s->break_cond);
    copy_field(out->log_text, sizeof(out->log_text), s->log_text);
    copy_field(out->log_cond, sizeof(out->log_cond), s->log_cond);
    copy_field(out->cmd, sizeof(out->cmd), s->cmd);
    copy_field(out->cmd_cond, sizeof(out->cmd_cond), s->cmd_cond);
}

static bool field_is(const char *field, const char *a, const char *b) {
    re_str_t f = re_str(field);
    return re_str_eq_cstr(f, a) || re_str_eq_cstr(f, b);
}

static bool parse_boolish(const char *value) {
    re_str_t v = re_str(value);
    if (v.n == 0)
        return false;
    if (re_str_eq_cstr(v, "0") || re_str_ieq_cstr(v, "false") || re_str_ieq_cstr(v, "no") ||
        re_str_ieq_cstr(v, "off"))
        return false;
    return true;
}

re_err_code_t re_dbg_bp_set_field(re_dbg_bps_t *bps, uint64_t addr, const char *field,
                                  const char *value) {
    re_dbg_bp_slot_t *s = find_slot(bps, addr);
    if (!s)
        return RE_E_NOTFOUND;
    if (!field || !field[0])
        return RE_E_USAGE;
    if (!value)
        value = "";
    if (field_is(field, "break", "break_cond"))
        copy_field(s->break_cond, sizeof(s->break_cond), value);
    else if (field_is(field, "log", "log_text"))
        copy_field(s->log_text, sizeof(s->log_text), value);
    else if (field_is(field, "logcond", "log_cond"))
        copy_field(s->log_cond, sizeof(s->log_cond), value);
    else if (field_is(field, "cmd", "command"))
        copy_field(s->cmd, sizeof(s->cmd), value);
    else if (field_is(field, "cmdcond", "cmd_cond"))
        copy_field(s->cmd_cond, sizeof(s->cmd_cond), value);
    else if (field_is(field, "fast", "fast_resume"))
        s->fast_resume = parse_boolish(value);
    else
        return RE_E_USAGE;
    return RE_OK;
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
        if (out && n < cap)
            fill_info(&bps->slot[i], &out[n]);
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

static size_t modrm_len(uint8_t modrm, const uint8_t *rest, size_t rest_n, bool rex_w_unused) {
    uint8_t mod = (uint8_t)(modrm >> 6);
    uint8_t rm = (uint8_t)(modrm & 7u);
    size_t n = 1;
    (void)rex_w_unused;
    if (mod == 3)
        return n;
    if (rm == 4) {
        if (rest_n < n + 1)
            return 0;
        n += 1; /* SIB */
    }
    if (mod == 1) {
        if (rest_n < n + 1)
            return 0;
        return n + 1;
    }
    if (mod == 2) {
        if (rest_n < n + 4)
            return 0;
        return n + 4;
    }
    /* mod == 0 */
    if (rm == 5) {
        if (rest_n < n + 4)
            return 0;
        return n + 4;
    }
    if (rm == 4 && rest_n >= 2) {
        uint8_t sib = rest[1];
        uint8_t base = (uint8_t)(sib & 7u);
        if (base == 5) {
            if (rest_n < n + 4)
                return 0;
            return n + 4;
        }
    }
    return n;
}

static re_err_code_t sess_note(re_dbg_session_t *s, re_err_code_t c, const char *msg) {
    if (!s)
        return c == RE_OK ? RE_E_INTERNAL : c;
    if (c == RE_OK)
        RE_ERR_OK(&s->last_err);
    else
        RE_ERR_SET(&s->last_err, c, msg);
    return c;
}

re_err_code_t re_dbg_session_bp_set(re_dbg_session_t *s, uint64_t addr) {
    re_err_code_t c;
    if (!s || !s->backend || !s->backend->vt)
        return sess_note(s, RE_E_INTERNAL, "debug session has no backend");
    if (!s->attached)
        return sess_note(s, RE_E_USAGE, "not attached");
    if (!s->bps)
        return sess_note(s, RE_E_INTERNAL, "breakpoint table missing");
    c = re_dbg_bp_set(s->bps, s->backend, addr, false);
    if (c != RE_OK)
        return sess_note(s, c, "bp set failed");
    return sess_note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_bp_clear(re_dbg_session_t *s, uint64_t addr) {
    re_err_code_t c;
    if (!s || !s->attached)
        return sess_note(s, RE_E_USAGE, "not attached");
    c = re_dbg_bp_clear(s->bps, s->backend, addr);
    if (c != RE_OK)
        return sess_note(s, c, "bp clear failed");
    return sess_note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_bp_enable(re_dbg_session_t *s, uint64_t addr) {
    re_err_code_t c;
    if (!s || !s->attached)
        return sess_note(s, RE_E_USAGE, "not attached");
    c = re_dbg_bp_enable(s->bps, s->backend, addr);
    if (c != RE_OK)
        return sess_note(s, c, "bp enable failed");
    return sess_note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_bp_disable(re_dbg_session_t *s, uint64_t addr) {
    re_err_code_t c;
    if (!s || !s->attached)
        return sess_note(s, RE_E_USAGE, "not attached");
    c = re_dbg_bp_disable(s->bps, s->backend, addr);
    if (c != RE_OK)
        return sess_note(s, c, "bp disable failed");
    return sess_note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_bp_list(re_dbg_session_t *s, re_dbg_bp_info_t *out, size_t cap,
                                     size_t *n_out) {
    re_err_code_t c;
    if (!s || !s->backend || !s->backend->vt)
        return sess_note(s, RE_E_INTERNAL, "debug session has no backend");
    if (!s->bps)
        return sess_note(s, RE_E_INTERNAL, "breakpoint table missing");
    c = re_dbg_bp_list(s->bps, out, cap, n_out);
    if (c != RE_OK)
        return sess_note(s, c, "bp list failed");
    return sess_note(s, RE_OK, NULL);
}

bool re_dbg_x86_is_call(const uint8_t *buf, size_t cap, size_t *len_out) {
    size_t i = 0;
    uint8_t rex = 0;
    if (!buf || !len_out || cap == 0)
        return false;
    *len_out = 0;
    while (i < cap && (buf[i] & 0xf0u) == 0x40u) {
        rex = buf[i];
        i++;
        if (i > 1)
            return false; /* only one REX */
    }
    if (i >= cap)
        return false;
    if (buf[i] == 0xe8u) {
        if (cap < i + 5)
            return false;
        *len_out = i + 5;
        return true;
    }
    if (buf[i] == 0xffu) {
        uint8_t modrm;
        size_t mlen;
        if (cap < i + 2)
            return false;
        modrm = buf[i + 1];
        if (((modrm >> 3) & 7u) != 2u)
            return false; /* /2 = call r/m */
        mlen = modrm_len(modrm, buf + i + 1, cap - (i + 1), (rex & 8u) != 0);
        if (mlen == 0)
            return false;
        *len_out = i + 1 + mlen;
        return true;
    }
    (void)rex;
    return false;
}
