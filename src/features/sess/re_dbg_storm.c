// re_dbg_storm.c - soft/BP-storm drain helpers for session wait_event.
// Module: feature (C11).
// Owns: peek, soft drain, BP peer resume, post_event, load-BP filter.
// Depends: re_dbg_storm.h, re_dbg_bp, re_dbg_advbp, re_dbg_lock, re_dbg_coalesce.
#include "features/sess/re_dbg_storm.h"

#include "features/bp/re_dbg_advbp.h"
#include "features/debug/re_dbg_bp.h"
#include "features/sess/re_dbg_coalesce.h"
#include "features/sess/re_dbg_lock.h"

#include <string.h>

void re_dbg_storm_merge_count(re_dbg_event_t *dst, const re_dbg_event_t *src, uint32_t *dropped) {
    int32_t prev = dst->code;
    uint32_t keep_tid = dst->tid;
    *dst = *src;
    dst->tid = keep_tid ? keep_tid : src->tid;
    if (prev < 1)
        prev = 1;
    if (src->code > 0)
        prev += src->code;
    else
        prev += 1;
    dst->code = prev;
    if (dropped)
        (*dropped)++;
}

bool re_dbg_storm_post(re_dbg_session_t *s, re_dbg_event_t *ev) {
    re_dbg_event_kind_t before;
    bool consumed = false;
    if (!ev)
        return false;
    before = ev->kind;
    if (s->bps && ev->kind == RE_DBG_EV_BREAKPOINT) {
        if (re_dbg_bp_on_hit(s->bps, s->backend, ev->tid, &ev->addr))
            (void)re_dbg_session_cond_on_break(s, ev);
    }
    if (s->advbp && (ev->kind == RE_DBG_EV_BREAKPOINT || ev->kind == RE_DBG_EV_STEP)) {
        if (re_dbg_advbp_on_break(s->advbp, s->backend, ev->tid, &ev->addr))
            ev->kind = RE_DBG_EV_BREAKPOINT;
    }
    if (s->bps && ev->kind == RE_DBG_EV_STEP) {
        (void)re_dbg_bp_after_step(s->bps, s->backend, &consumed);
        if (consumed)
            ev->kind = RE_DBG_EV_NONE;
    }
    if (ev->kind == RE_DBG_EV_EXCEPTION)
        (void)re_dbg_ex_filter(s->bps, s->backend, ev);
    return consumed || (before != RE_DBG_EV_NONE && ev->kind == RE_DBG_EV_NONE);
}

bool re_dbg_storm_load_bp_stop(re_dbg_session_t *s, re_dbg_event_t *ev) {
    static re_dbg_module_t mods[RE_DBG_MAX_MODULES];
    size_t n = 0, i;
    if (!s->advbp || !re_dbg_advbp_has_load(s->advbp))
        return true;
    if (ev->kind == RE_DBG_EV_BREAK && re_dbg_advbp_is_rendezvous(s->advbp, ev->addr)) {
        ev->kind = RE_DBG_EV_LOAD_MODULE;
        return true;
    }
    if (ev->kind != RE_DBG_EV_LOAD_MODULE)
        return true;
    if (re_dbg_session_enum_modules(s, mods, RE_DBG_MAX_MODULES, &n) != RE_OK)
        return true;
    for (i = 0; i < n; i++) {
        if (mods[i].base == ev->addr && re_dbg_advbp_load_matches(s->advbp, mods[i].path))
            return true;
    }
    return !re_dbg_advbp_has_load(s->advbp);
}

re_err_code_t re_dbg_storm_peek(re_dbg_session_t *s, re_dbg_event_t *nxt, bool release_pending) {
    re_err_code_t c;
    memset(nxt, 0, sizeof(*nxt));
    /* Linux: timeout 0 blocks forever; 1 ms uses WNOHANG. */
    re_dbg_lock_before_wait();
    c = s->backend->vt->wait_event(s->backend, nxt, 1);
    re_dbg_lock_after_wait();
    if (c != RE_E_USAGE)
        return c;
    if (!release_pending || !s->backend->vt->cont) {
        nxt->kind = RE_DBG_EV_NONE;
        return RE_OK;
    }
    (void)s->backend->vt->cont(s->backend);
    re_dbg_lock_before_wait();
    c = s->backend->vt->wait_event(s->backend, nxt, 1);
    re_dbg_lock_after_wait();
    if (c == RE_E_USAGE) {
        nxt->kind = RE_DBG_EV_NONE;
        return RE_OK;
    }
    return c;
}

re_err_code_t re_dbg_storm_drain_soft(re_dbg_session_t *s, re_dbg_event_t *ev, uint32_t *dropped) {
    size_t i;
    for (i = 0; i < 32u; i++) {
        re_dbg_event_t nxt;
        re_err_code_t c = re_dbg_storm_peek(s, &nxt, true);
        bool skipped;
        if (c != RE_OK)
            return c;
        if (nxt.kind == RE_DBG_EV_NONE)
            break;
        skipped = re_dbg_storm_post(s, &nxt);
        if (nxt.kind == RE_DBG_EV_NONE) {
            if (skipped)
                continue;
            break;
        }
        if (!re_dbg_storm_load_bp_stop(s, &nxt)) {
            if (s->backend->vt->cont)
                (void)s->backend->vt->cont(s->backend);
            continue;
        }
        if (nxt.kind == RE_DBG_EV_EXIT || !re_dbg_event_is_soft(nxt.kind)) {
            *ev = nxt;
            return RE_OK;
        }
        if (ev->kind == nxt.kind)
            re_dbg_storm_merge_count(ev, &nxt, dropped);
        else {
            if (dropped)
                (*dropped)++;
            *ev = nxt;
            if (ev->code < 1)
                ev->code = 1;
        }
    }
    return RE_OK;
}

re_err_code_t re_dbg_storm_drain_bp(re_dbg_session_t *s, re_dbg_event_t *ev, uint32_t *dropped) {
    size_t i;
    uint32_t primary_tid = ev->tid;
    if (ev->code < 1)
        ev->code = 1;
    for (i = 0; i < 64u; i++) {
        re_dbg_event_t nxt;
        re_err_code_t c;
        bool need = false, rtc = false;
        uint64_t ra = 0;
        c = re_dbg_storm_peek(s, &nxt, false);
        if (c != RE_OK)
            return c;
        if (nxt.kind == RE_DBG_EV_NONE)
            break;
        if (s->bps) {
            need = s->bps->need_rearm;
            ra = s->bps->rearm_addr;
            rtc = s->bps->rearm_then_cont;
        }
        (void)re_dbg_storm_post(s, &nxt);
        if (nxt.kind == RE_DBG_EV_NONE)
            continue;
        if (nxt.kind == RE_DBG_EV_EXIT) {
            *ev = nxt;
            return RE_OK;
        }
        if (re_dbg_event_is_soft(nxt.kind)) {
            if (s->backend->vt->cont)
                (void)s->backend->vt->cont(s->backend);
            if (dropped)
                (*dropped)++;
            continue;
        }
        if (!re_dbg_event_is_bp_storm(ev, &nxt)) {
            *ev = nxt;
            return RE_OK;
        }
        if (s->bps) {
            s->bps->need_rearm = need || s->bps->need_rearm;
            if (ra)
                s->bps->rearm_addr = ra;
            s->bps->rearm_then_cont = rtc;
        }
        if (s->backend->vt->cont)
            (void)s->backend->vt->cont(s->backend);
        re_dbg_storm_merge_count(ev, &nxt, dropped);
        ev->tid = primary_tid;
        s->bp_storms++;
    }
    return RE_OK;
}
