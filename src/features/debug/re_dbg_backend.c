// re_dbg_backend.c - host OS backend factory, caps helpers, exception policy.
// Module: feature (C11).
// Owns: backend selection, capability queries, and exception/signal rule table.
// Depends: re_dbg.h, re_dbg_bp.h. Both OS backends compile; one is selected.
#include "features/debug/re_dbg_bp.h"

#include <string.h>

re_dbg_backend_t *re_dbg_backend_linux_create(re_arena_t *a);
re_dbg_backend_t *re_dbg_backend_win_create(re_arena_t *a);

re_dbg_backend_t *re_dbg_backend_create(re_arena_t *a) {
#if defined(_WIN32)
    return re_dbg_backend_win_create(a);
#else
    return re_dbg_backend_linux_create(a);
#endif
}

re_err_code_t re_dbg_ex_set(re_dbg_bps_t *bps, int32_t lo, int32_t hi, re_dbg_ex_policy_t policy,
                            bool first_chance_only) {
    size_t i;
    re_dbg_ex_rule_t *slot = NULL;
    if (!bps)
        return RE_E_INTERNAL;
    if (hi < lo)
        return RE_E_USAGE;
    for (i = 0; i < RE_DBG_MAX_EX; i++) {
        if (bps->ex[i].used && bps->ex[i].code_lo == lo && bps->ex[i].code_hi == hi) {
            slot = &bps->ex[i];
            break;
        }
        if (!slot && !bps->ex[i].used)
            slot = &bps->ex[i];
    }
    if (!slot)
        return RE_E_RANGE;
    slot->used = true;
    slot->code_lo = lo;
    slot->code_hi = hi;
    slot->policy = policy;
    slot->first_chance_only = first_chance_only;
    return RE_OK;
}

re_err_code_t re_dbg_ex_clear(re_dbg_bps_t *bps, int32_t lo, int32_t hi) {
    size_t i;
    bool found = false;
    if (!bps)
        return RE_E_INTERNAL;
    for (i = 0; i < RE_DBG_MAX_EX; i++) {
        if (!bps->ex[i].used)
            continue;
        if (bps->ex[i].code_lo == lo && bps->ex[i].code_hi == hi) {
            memset(&bps->ex[i], 0, sizeof(bps->ex[i]));
            found = true;
        }
    }
    return found ? RE_OK : RE_E_NOTFOUND;
}

re_err_code_t re_dbg_ex_list(const re_dbg_bps_t *bps, re_dbg_ex_rule_t *out, size_t cap,
                             size_t *n_out) {
    size_t i, n = 0;
    if (n_out)
        *n_out = 0;
    if (!bps)
        return RE_E_INTERNAL;
    for (i = 0; i < RE_DBG_MAX_EX; i++) {
        if (!bps->ex[i].used)
            continue;
        if (out && n < cap)
            out[n] = bps->ex[i];
        n++;
    }
    if (n_out)
        *n_out = n;
    return RE_OK;
}

re_dbg_ex_policy_t re_dbg_ex_lookup(const re_dbg_bps_t *bps, int32_t code, bool first_chance) {
    size_t i;
    if (!bps)
        return RE_DBG_EX_BREAK_PASS;
    for (i = 0; i < RE_DBG_MAX_EX; i++) {
        if (!bps->ex[i].used)
            continue;
        if (code < bps->ex[i].code_lo || code > bps->ex[i].code_hi)
            continue;
        if (bps->ex[i].first_chance_only && !first_chance)
            continue;
        return bps->ex[i].policy;
    }
    return RE_DBG_EX_BREAK_PASS;
}

static re_err_code_t apply_pass(re_dbg_backend_t *b, bool pass, int32_t code) {
    if (!b || !b->vt)
        return RE_E_INTERNAL;
    if (!b->vt->set_pass)
        return RE_OK;
    return b->vt->set_pass(b, pass, code);
}

bool re_dbg_ex_filter(re_dbg_bps_t *bps, re_dbg_backend_t *b, re_dbg_event_t *ev) {
    re_dbg_ex_policy_t pol;
    bool pass;
    if (!bps || !b || !ev || ev->kind != RE_DBG_EV_EXCEPTION)
        return false;
    pol = re_dbg_ex_lookup(bps, ev->code, ev->first_chance);
    if (pol == RE_DBG_EX_BREAK_PASS || pol == RE_DBG_EX_BREAK_HANDLE) {
        bps->pending_policy = pol;
        bps->pending_code = ev->code;
        bps->pending_valid = true;
        return false;
    }
    pass = (pol == RE_DBG_EX_PASS);
    if (apply_pass(b, pass, ev->code) != RE_OK)
        return false;
    if (!b->vt->cont || b->vt->cont(b) != RE_OK)
        return false;
    bps->pending_valid = false;
    ev->kind = RE_DBG_EV_NONE;
    return true;
}

re_err_code_t re_dbg_ex_prepare_cont(re_dbg_bps_t *bps, re_dbg_backend_t *b) {
    bool pass;
    re_err_code_t c;
    if (!bps || !b)
        return RE_E_INTERNAL;
    if (!bps->pending_valid)
        return RE_OK;
    pass = (bps->pending_policy == RE_DBG_EX_BREAK_PASS);
    c = apply_pass(b, pass, bps->pending_code);
    bps->pending_valid = false;
    return c;
}

const char *re_dbg_event_kind_str(re_dbg_event_kind_t kind) {
    switch (kind) {
        case RE_DBG_EV_NONE:
            return "none";
        case RE_DBG_EV_CREATE:
            return "create";
        case RE_DBG_EV_EXIT:
            return "exit";
        case RE_DBG_EV_BREAKPOINT:
            return "breakpoint";
        case RE_DBG_EV_STEP:
            return "step";
        case RE_DBG_EV_EXCEPTION:
            return "exception";
        case RE_DBG_EV_LOAD_MODULE:
            return "load_module";
        case RE_DBG_EV_UNLOAD_MODULE:
            return "unload";
        case RE_DBG_EV_THREAD_CREATE:
            return "thread_create";
        case RE_DBG_EV_THREAD_EXIT:
            return "thread_exit";
        case RE_DBG_EV_OUTPUT:
            return "output";
        default:
            return "unknown";
    }
}

const char *re_dbg_ex_policy_str(re_dbg_ex_policy_t p) {
    switch (p) {
        case RE_DBG_EX_BREAK_PASS:
            return "break_pass";
        case RE_DBG_EX_BREAK_HANDLE:
            return "break_handle";
        case RE_DBG_EX_PASS:
            return "pass";
        case RE_DBG_EX_IGNORE:
            return "ignore";
        default:
            return "unknown";
    }
}

re_dbg_caps_t re_dbg_backend_caps(const re_dbg_backend_t *b) {
    re_dbg_caps_t z;
    memset(&z, 0, sizeof(z));
    return b ? b->caps : z;
}

re_dbg_caps_t re_dbg_session_caps(const re_dbg_session_t *s) {
    return re_dbg_backend_caps(s ? s->backend : NULL);
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

re_err_code_t re_dbg_session_ex_set(re_dbg_session_t *s, int32_t lo, int32_t hi,
                                    re_dbg_ex_policy_t policy, bool first_chance_only) {
    re_err_code_t c;
    if (!s || !s->backend || !s->backend->vt)
        return RE_E_INTERNAL;
    if (!s->bps)
        return sess_note(s, RE_E_INTERNAL, "exception table missing");
    c = re_dbg_ex_set(s->bps, lo, hi, policy, first_chance_only);
    return sess_note(s, c, c == RE_OK ? NULL : "exception rule set failed");
}

re_err_code_t re_dbg_session_ex_clear(re_dbg_session_t *s, int32_t lo, int32_t hi) {
    re_err_code_t c;
    if (!s || !s->backend || !s->backend->vt)
        return RE_E_INTERNAL;
    if (!s->bps)
        return sess_note(s, RE_E_INTERNAL, "exception table missing");
    c = re_dbg_ex_clear(s->bps, lo, hi);
    return sess_note(s, c, c == RE_OK ? NULL : "exception rule clear failed");
}

re_err_code_t re_dbg_session_ex_list(re_dbg_session_t *s, re_dbg_ex_rule_t *out, size_t cap,
                                     size_t *n_out) {
    re_err_code_t c;
    if (!s || !s->backend || !s->backend->vt)
        return RE_E_INTERNAL;
    if (!s->bps)
        return sess_note(s, RE_E_INTERNAL, "exception table missing");
    c = re_dbg_ex_list(s->bps, out, cap, n_out);
    return sess_note(s, c, c == RE_OK ? NULL : "exception list failed");
}
