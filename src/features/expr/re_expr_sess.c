// re_expr_sess.c - session $vars, watches, eval, and conditional BP policy.
// Module: feature/expr (C11).
// Owns: var/watch tables, pending BP commands, and break/log/cmd decisions.
// Depends: re_dbg.h, re_dbg_bp.h, re_expr.h.
#include "features/debug/re_dbg.h"
#include "features/debug/re_dbg_bp.h"
#include "features/expr/re_expr.h"
#include "features/script/re_script.h"

#include "utils/text/re_str.h"

#include <string.h>

static void copy_str(char *dst, size_t cap, const char *src) {
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

static void push_bp_log(re_dbg_session_t *s, const char *text) {
    if (!s || !text || !text[0])
        return;
    (void)re_dbg_session_log_add(s, text);
}

re_err_code_t re_dbg_session_eval(re_dbg_session_t *s, const char *expr, int64_t *out) {
    char err[RE_EXPR_ERR_MAX];
    re_err_code_t c;
    if (!s || !expr || !out)
        return RE_E_USAGE;
    c = re_expr_eval(s, expr, out, err, sizeof(err));
    if (c != RE_OK && err[0])
        RE_ERR_SET(&s->last_err, c, err);
    return c;
}

re_err_code_t re_dbg_session_var_get(re_dbg_session_t *s, const char *name, int64_t *out) {
    size_t i;
    re_str_t n;
    if (!s || !name || !out)
        return RE_E_USAGE;
    n = re_str(name);
    if (re_str_ieq_cstr(n, "hitcount") || re_str_ieq_cstr(n, "breakpointcounter")) {
        *out = (int64_t)s->eval_hit_count;
        return RE_OK;
    }
    for (i = 0; i < RE_DBG_MAX_VARS; i++) {
        if (s->vars[i].used && re_str_ieq_cstr(n, s->vars[i].name)) {
            *out = s->vars[i].value;
            return RE_OK;
        }
    }
    return RE_E_NOTFOUND;
}

re_err_code_t re_dbg_session_var_set(re_dbg_session_t *s, const char *name, int64_t value) {
    size_t i;
    re_str_t n;
    if (!s || !name || !name[0])
        return RE_E_USAGE;
    n = re_str(name);
    for (i = 0; i < RE_DBG_MAX_VARS; i++) {
        if (s->vars[i].used && re_str_ieq_cstr(n, s->vars[i].name)) {
            s->vars[i].value = value;
            return RE_OK;
        }
    }
    for (i = 0; i < RE_DBG_MAX_VARS; i++) {
        if (s->vars[i].used)
            continue;
        copy_str(s->vars[i].name, sizeof(s->vars[i].name), name);
        s->vars[i].value = value;
        s->vars[i].used = true;
        return RE_OK;
    }
    return RE_E_RANGE;
}

re_err_code_t re_dbg_session_watch_add(re_dbg_session_t *s, const char *expr) {
    size_t i;
    if (!s || !expr || !expr[0])
        return RE_E_USAGE;
    for (i = 0; i < RE_DBG_MAX_WATCH; i++) {
        if (s->watches[i].used)
            continue;
        copy_str(s->watches[i].expr, sizeof(s->watches[i].expr), expr);
        s->watches[i].used = true;
        return RE_OK;
    }
    return RE_E_RANGE;
}

re_err_code_t re_dbg_session_watch_clear(re_dbg_session_t *s, size_t index_or_all) {
    size_t i, n = 0;
    if (!s)
        return RE_E_INTERNAL;
    if (index_or_all == (size_t)-1) {
        memset(s->watches, 0, sizeof(s->watches));
        return RE_OK;
    }
    for (i = 0; i < RE_DBG_MAX_WATCH; i++) {
        if (!s->watches[i].used)
            continue;
        if (n == index_or_all) {
            memset(&s->watches[i], 0, sizeof(s->watches[i]));
            return RE_OK;
        }
        n++;
    }
    return RE_E_NOTFOUND;
}

re_err_code_t re_dbg_session_watch_list(re_dbg_session_t *s, re_dbg_watch_t *out, size_t cap,
                                        size_t *n_out) {
    size_t i, n = 0;
    if (n_out)
        *n_out = 0;
    if (!s)
        return RE_E_INTERNAL;
    for (i = 0; i < RE_DBG_MAX_WATCH; i++) {
        if (!s->watches[i].used)
            continue;
        if (out && n < cap)
            out[n] = s->watches[i];
        n++;
    }
    if (n_out)
        *n_out = n;
    return RE_OK;
}

re_err_code_t re_dbg_session_watch_eval(re_dbg_session_t *s, size_t index, int64_t *value_out,
                                        char *err, size_t err_cap) {
    size_t i, n = 0;
    if (!s || !value_out)
        return RE_E_USAGE;
    for (i = 0; i < RE_DBG_MAX_WATCH; i++) {
        if (!s->watches[i].used)
            continue;
        if (n == index)
            return re_expr_eval(s, s->watches[i].expr, value_out, err, err_cap);
        n++;
    }
    return RE_E_NOTFOUND;
}

re_err_code_t re_dbg_session_bp_set_cond(re_dbg_session_t *s, uint64_t addr, const char *field,
                                         const char *value) {
    re_err_code_t c;
    if (!s || !s->bps)
        return RE_E_INTERNAL;
    c = re_dbg_bp_set_field(s->bps, addr, field, value);
    if (c != RE_OK)
        RE_ERR_SET(&s->last_err, c, "bp condition field update failed");
    return c;
}

static bool truth(re_dbg_session_t *s, const char *expr, bool default_true) {
    int64_t v = 0;
    if (!expr || !expr[0])
        return default_true;
    if (re_dbg_session_eval(s, expr, &v) != RE_OK)
        return default_true;
    return v != 0;
}

static bool schedule_fast_resume(re_dbg_session_t *s) {
    re_err_code_t c;
    if (!s || !s->bps || !s->backend)
        return false;
    c = re_dbg_bp_prepare_cont(s->bps, s->backend);
    return c == RE_OK;
}

bool re_dbg_session_cond_on_break(re_dbg_session_t *s, re_dbg_event_t *ev) {
    re_dbg_bp_slot_t *slot;
    bool do_break;
    if (!s || !ev || !s->bps)
        return true;
    slot = re_dbg_bp_find(s->bps, ev->addr);
    if (!slot)
        return true;
    slot->hit_count++;
    s->eval_hit_count = slot->hit_count;
    do_break = truth(s, slot->break_cond, true);
    if (slot->fast_resume && !do_break) {
        if (schedule_fast_resume(s)) {
            ev->kind = RE_DBG_EV_NONE;
            s->pending_cmd[0] = 0;
            return false;
        }
    }
    if (slot->log_text[0] && truth(s, slot->log_cond, true))
        push_bp_log(s, slot->log_text);
    if (slot->cmd[0] && truth(s, slot->cmd_cond, true))
        copy_str(s->pending_cmd, sizeof(s->pending_cmd), slot->cmd);
    else
        s->pending_cmd[0] = 0;
    if (!do_break) {
        if (schedule_fast_resume(s)) {
            ev->kind = RE_DBG_EV_NONE;
            return false;
        }
    }
    return true;
}
