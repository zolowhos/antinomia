// re_dbg_coalesce.c - soft/BP-storm coalesce and session wait_event body.
// Module: feature (C11).
// Owns: wait loop, EXIT detach, and soft/BP storm collapse orchestration.
// Depends: re_dbg_coalesce.h, re_dbg_storm, re_dbg_bp, re_dbg_lock, re_dbg_trace.
#include "features/sess/re_dbg_coalesce.h"

#include "features/bp/re_dbg_advbp.h"
#include "features/debug/re_dbg_bp.h"
#include "features/sess/re_dbg_lock.h"
#include "features/sess/re_dbg_storm.h"
#include "features/trace/re_dbg_trace.h"

#include <string.h>

bool re_dbg_event_is_soft(re_dbg_event_kind_t kind) {
    return kind == RE_DBG_EV_THREAD_CREATE || kind == RE_DBG_EV_THREAD_EXIT ||
           kind == RE_DBG_EV_LOAD_MODULE || kind == RE_DBG_EV_UNLOAD_MODULE ||
           kind == RE_DBG_EV_OUTPUT;
}

bool re_dbg_event_is_bp_storm(const re_dbg_event_t *a, const re_dbg_event_t *b) {
    if (!a || !b)
        return false;
    if (a->kind != RE_DBG_EV_BREAKPOINT || b->kind != RE_DBG_EV_BREAKPOINT)
        return false;
    return a->addr == b->addr;
}

size_t re_dbg_coalesce_events(re_dbg_event_t *evs, size_t n, uint32_t *dropped) {
    size_t w = 0, r;
    uint32_t drop = 0;
    if (dropped)
        *dropped = 0;
    if (!evs || n == 0)
        return 0;
    for (r = 0; r < n; r++) {
        re_dbg_event_t *cur = &evs[r];
        if (w > 0 && re_dbg_event_is_soft(cur->kind) && evs[w - 1].kind == cur->kind) {
            /* Soft: keep newest payload; bump collapse count. */
            int32_t prev = evs[w - 1].code;
            evs[w - 1] = *cur;
            if (prev < 1)
                prev = 1;
            if (cur->code > 0)
                prev += cur->code;
            else
                prev += 1;
            evs[w - 1].code = prev;
            drop++;
            continue;
        }
        if (w > 0 && re_dbg_event_is_bp_storm(&evs[w - 1], cur)) {
            /* BP storm: keep first stopper tid for UI focus. */
            re_dbg_storm_merge_count(&evs[w - 1], cur, &drop);
            continue;
        }
        evs[w++] = *cur;
    }
    if (dropped)
        *dropped = drop;
    return w;
}

static re_err_code_t note(re_dbg_session_t *s, re_err_code_t c, const char *msg) {
    if (!s)
        return c == RE_OK ? RE_E_INTERNAL : c;
    if (c == RE_OK)
        RE_ERR_OK(&s->last_err);
    else
        RE_ERR_SET(&s->last_err, c, msg);
    return c;
}

static void apply_event(re_dbg_session_t *s, re_dbg_event_t *ev) {
    s->last_event = *ev;
    if (ev->pid)
        s->pid = ev->pid;
    if (ev->tid && ev->kind != RE_DBG_EV_THREAD_CREATE && ev->kind != RE_DBG_EV_THREAD_EXIT)
        s->tid = ev->tid;
    if (ev->kind == RE_DBG_EV_EXIT) {
        s->exit_code = ev->code;
        s->attached = false;
        if (s->bps)
            (void)re_dbg_bp_disarm_all(s->bps, s->backend);
        if (s->advbp)
            (void)re_dbg_advbp_disarm_all(s->advbp, s->backend);
    }
    if (s->trace && s->trace->recording &&
        (ev->kind == RE_DBG_EV_STEP || ev->kind == RE_DBG_EV_BREAKPOINT))
        (void)re_dbg_session_trace_capture(s);
}

static re_err_code_t after_hard(re_dbg_session_t *s, re_dbg_event_t *ev, uint32_t *dropped) {
    re_err_code_t c = RE_OK;
    if (ev->kind == RE_DBG_EV_BREAKPOINT) {
        c = re_dbg_storm_drain_bp(s, ev, dropped);
        if (c == RE_OK)
            s->events_coalesced += *dropped;
    }
    return c;
}

re_err_code_t re_dbg_coalesce_wait(re_dbg_session_t *s, re_dbg_event_t *ev, uint32_t timeout_ms) {
    re_dbg_event_t local;
    re_err_code_t c;
    if (!s)
        return RE_E_INTERNAL;
    if (!ev)
        ev = &local;
    memset(ev, 0, sizeof(*ev));
    if (!s->backend || !s->backend->vt)
        return note(s, RE_E_INTERNAL, "debug session has no backend");
    if (!s->backend->vt->wait_event)
        return note(s, RE_E_UNSUPPORTED, "wait_event is not available on this backend");
    for (;;) {
        bool skipped;
        uint32_t dropped = 0;
        re_dbg_lock_before_wait();
        c = s->backend->vt->wait_event(s->backend, ev, timeout_ms);
        re_dbg_lock_after_wait();
        if (c != RE_OK)
            return note(s, c, "wait_event failed or is not implemented");
        skipped = re_dbg_storm_post(s, ev);
        if (ev->kind != RE_DBG_EV_NONE && !re_dbg_storm_load_bp_stop(s, ev)) {
            if (s->backend->vt->cont)
                (void)s->backend->vt->cont(s->backend);
            continue;
        }
        if (ev->kind == RE_DBG_EV_NONE) {
            if (skipped || timeout_ms == 0)
                continue;
            break;
        }
        if (ev->kind == RE_DBG_EV_EXIT)
            break;
        if (re_dbg_event_is_soft(ev->kind)) {
            if (ev->code < 1)
                ev->code = 1;
            c = re_dbg_storm_drain_soft(s, ev, &dropped);
            if (c != RE_OK)
                return note(s, c, "wait_event failed or is not implemented");
            s->events_coalesced += dropped;
            if (ev->kind == RE_DBG_EV_EXIT)
                break;
            dropped = 0;
            c = after_hard(s, ev, &dropped);
            if (c != RE_OK)
                return note(s, c, "wait_event failed or is not implemented");
            break;
        }
        c = after_hard(s, ev, &dropped);
        if (c != RE_OK)
            return note(s, c, "wait_event failed or is not implemented");
        break;
    }
    apply_event(s, ev);
    return note(s, RE_OK, NULL);
}
