// re_dbg_coalesce.c - soft-event coalesce and session wait_event body.
// Module: feature (C11).
// Owns: wait loop post-process plus THREAD/LOAD/OUTPUT storm collapse.
// Depends: re_dbg_coalesce.h, re_dbg_bp, re_dbg_advbp, re_dbg_lock, re_dbg_trace.
#include "features/sess/re_dbg_coalesce.h"

#include "features/bp/re_dbg_advbp.h"
#include "features/debug/re_dbg_bp.h"
#include "features/sess/re_dbg_lock.h"
#include "features/trace/re_dbg_trace.h"

#include <string.h>

bool re_dbg_event_is_soft(re_dbg_event_kind_t kind) {
    return kind == RE_DBG_EV_THREAD_CREATE || kind == RE_DBG_EV_THREAD_EXIT ||
           kind == RE_DBG_EV_LOAD_MODULE || kind == RE_DBG_EV_UNLOAD_MODULE ||
           kind == RE_DBG_EV_OUTPUT;
}

size_t re_dbg_coalesce_events(re_dbg_event_t *evs, size_t n, uint32_t *dropped) {
    size_t w = 0;
    size_t r;
    uint32_t drop = 0;
    if (dropped) {
        *dropped = 0;
    }
    if (!evs || n == 0) {
        return 0;
    }
    for (r = 0; r < n; r++) {
        re_dbg_event_t *cur = &evs[r];
        if (w > 0 && re_dbg_event_is_soft(cur->kind) && evs[w - 1].kind == cur->kind) {
            /* Keep the newest soft of this kind; bump code with collapse count. */
            int32_t prev = evs[w - 1].code;
            evs[w - 1] = *cur;
            if (prev < 1) {
                prev = 1;
            }
            if (cur->code > 0) {
                prev += cur->code;
            } else {
                prev += 1;
            }
            evs[w - 1].code = prev;
            drop++;
            continue;
        }
        evs[w++] = *cur;
    }
    if (dropped) {
        *dropped = drop;
    }
    return w;
}

static re_err_code_t note(re_dbg_session_t *s, re_err_code_t c, const char *msg) {
    if (!s) {
        return c == RE_OK ? RE_E_INTERNAL : c;
    }
    if (c == RE_OK) {
        RE_ERR_OK(&s->last_err);
    } else {
        RE_ERR_SET(&s->last_err, c, msg);
    }
    return c;
}

/* Returns true when a soft-BP rearm STEP was swallowed and the inferior resumed. */
static bool post_event(re_dbg_session_t *s, re_dbg_event_t *ev) {
    bool consumed = false;
    bool ours = false;
    if (!ev) {
        return false;
    }
    if (s->bps && ev->kind == RE_DBG_EV_BREAKPOINT) {
        ours = re_dbg_bp_on_hit(s->bps, s->backend, ev->tid, &ev->addr);
        if (ours) {
            (void)re_dbg_session_cond_on_break(s, ev);
        }
    }
    if (s->advbp && (ev->kind == RE_DBG_EV_BREAKPOINT || ev->kind == RE_DBG_EV_STEP)) {
        if (re_dbg_advbp_on_break(s->advbp, s->backend, ev->tid, &ev->addr)) {
            ev->kind = RE_DBG_EV_BREAKPOINT;
        }
    }
    if (s->bps && ev->kind == RE_DBG_EV_STEP) {
        (void)re_dbg_bp_after_step(s->bps, s->backend, &consumed);
        if (consumed) {
            ev->kind = RE_DBG_EV_NONE;
        }
    }
    if (ev->kind == RE_DBG_EV_EXCEPTION) {
        (void)re_dbg_ex_filter(s->bps, s->backend, ev);
    }
    return consumed;
}

static bool load_bp_should_stop(re_dbg_session_t *s, re_dbg_event_t *ev) {
    re_dbg_module_t mods[64];
    size_t n = 0, i;
    if (!s->advbp || !re_dbg_advbp_has_load(s->advbp)) {
        return true;
    }
    if (ev->kind == RE_DBG_EV_BREAK && re_dbg_advbp_is_rendezvous(s->advbp, ev->addr)) {
        ev->kind = RE_DBG_EV_LOAD_MODULE;
        return true;
    }
    if (ev->kind != RE_DBG_EV_LOAD_MODULE) {
        return true;
    }
    if (re_dbg_session_enum_modules(s, mods, 64, &n) != RE_OK) {
        return true;
    }
    for (i = 0; i < n; i++) {
        if (mods[i].base == ev->addr && re_dbg_advbp_load_matches(s->advbp, mods[i].path)) {
            return true;
        }
    }
    return !re_dbg_advbp_has_load(s->advbp);
}

static void apply_event(re_dbg_session_t *s, re_dbg_event_t *ev) {
    s->last_event = *ev;
    if (ev->pid) {
        s->pid = ev->pid;
    }
    /* Do not adopt THREAD_CREATE tids: Linux auto-continues the new thread. */
    if (ev->tid && ev->kind != RE_DBG_EV_THREAD_CREATE && ev->kind != RE_DBG_EV_THREAD_EXIT) {
        s->tid = ev->tid;
    }
    if (s->trace && s->trace->recording &&
        (ev->kind == RE_DBG_EV_STEP || ev->kind == RE_DBG_EV_BREAKPOINT)) {
        (void)re_dbg_session_trace_capture(s);
    }
}

static void merge_soft(re_dbg_event_t *dst, const re_dbg_event_t *src, uint32_t *dropped) {
    int32_t prev = dst->code;
    *dst = *src;
    if (prev < 1) {
        prev = 1;
    }
    if (src->code > 0) {
        prev += src->code;
    } else {
        prev += 1;
    }
    dst->code = prev;
    if (dropped) {
        (*dropped)++;
    }
}

static re_err_code_t drain_soft(re_dbg_session_t *s, re_dbg_event_t *ev, uint32_t *dropped) {
    size_t i;
    for (i = 0; i < 32u; i++) {
        re_dbg_event_t nxt;
        re_err_code_t c;
        bool skipped;
        memset(&nxt, 0, sizeof(nxt));
        re_dbg_lock_before_wait();
        c = s->backend->vt->wait_event(s->backend, &nxt, 0);
        re_dbg_lock_after_wait();
        if (c != RE_OK) {
            return c;
        }
        if (nxt.kind == RE_DBG_EV_NONE) {
            break;
        }
        skipped = post_event(s, &nxt);
        if (nxt.kind == RE_DBG_EV_NONE) {
            if (skipped) {
                continue;
            }
            break;
        }
        if (!load_bp_should_stop(s, &nxt)) {
            if (s->backend->vt->cont) {
                (void)s->backend->vt->cont(s->backend);
            }
            continue;
        }
        if (!re_dbg_event_is_soft(nxt.kind)) {
            /* Hard stop wins; discard the soft summary. */
            *ev = nxt;
            return RE_OK;
        }
        if (ev->kind == nxt.kind) {
            merge_soft(ev, &nxt, dropped);
        } else {
            /* Different soft kind: keep the newest; count the prior as dropped. */
            if (dropped) {
                (*dropped)++;
            }
            *ev = nxt;
            if (ev->code < 1) {
                ev->code = 1;
            }
        }
    }
    return RE_OK;
}

re_err_code_t re_dbg_coalesce_wait(re_dbg_session_t *s, re_dbg_event_t *ev, uint32_t timeout_ms) {
    re_dbg_event_t local;
    re_err_code_t c;
    if (!s) {
        return RE_E_INTERNAL;
    }
    if (!ev) {
        ev = &local;
    }
    memset(ev, 0, sizeof(*ev));
    if (!s->backend || !s->backend->vt) {
        return note(s, RE_E_INTERNAL, "debug session has no backend");
    }
    if (!s->backend->vt->wait_event) {
        return note(s, RE_E_UNSUPPORTED, "wait_event is not available on this backend");
    }
    for (;;) {
        bool skipped;
        uint32_t dropped = 0;
        re_dbg_lock_before_wait();
        c = s->backend->vt->wait_event(s->backend, ev, timeout_ms);
        re_dbg_lock_after_wait();
        if (c != RE_OK) {
            return note(s, c, "wait_event failed or is not implemented");
        }
        skipped = post_event(s, ev);
        if (ev->kind != RE_DBG_EV_NONE && !load_bp_should_stop(s, ev)) {
            if (s->backend->vt->cont) {
                (void)s->backend->vt->cont(s->backend);
            }
            continue;
        }
        if (ev->kind == RE_DBG_EV_NONE) {
            if (skipped) {
                continue;
            }
            if (timeout_ms != 0) {
                break;
            }
            continue;
        }
        if (re_dbg_event_is_soft(ev->kind)) {
            if (ev->code < 1) {
                ev->code = 1;
            }
            c = drain_soft(s, ev, &dropped);
            if (c != RE_OK) {
                return note(s, c, "wait_event failed or is not implemented");
            }
            s->events_coalesced += dropped;
        }
        break;
    }
    apply_event(s, ev);
    return note(s, RE_OK, NULL);
}
