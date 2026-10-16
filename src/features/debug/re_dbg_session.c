// re_dbg_session.c - long-lived debug session arena and control API.
// Module: feature (C11).
// Owns: session init/reset/free, BP/step/pause wrappers, wait post-process.
// Depends: re_dbg.h, re_dbg_bp.h, re_dbg_x86.h. Session arena is not per-command.
#include "features/debug/re_dbg.h"
#include "features/debug/re_dbg_bp.h"
#include "features/debug/re_dbg_x86.h"

#include <string.h>

static re_dbg_session_t g_session;
static bool g_ready;

static void clear_target(re_dbg_session_t *s) {
    s->attached = false;
    s->pid = 0;
    s->tid = 0;
    s->path[0] = 0;
    memset(&s->last_event, 0, sizeof(s->last_event));
}

static void copy_path(re_dbg_session_t *s, const char *path) {
    size_t i = 0;
    if (!path) {
        s->path[0] = 0;
        return;
    }
    while (path[i] && i + 1u < sizeof(s->path)) {
        s->path[i] = path[i];
        i++;
    }
    s->path[i] = 0;
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

static re_err_code_t need_backend(re_dbg_session_t *s) {
    if (!s)
        return RE_E_INTERNAL;
    if (!s->backend || !s->backend->vt)
        return note(s, RE_E_INTERNAL, "debug session has no backend");
    return RE_OK;
}

static re_err_code_t need_attached(re_dbg_session_t *s) {
    re_err_code_t c = need_backend(s);
    if (c != RE_OK)
        return c;
    if (!s->attached)
        return note(s, RE_E_USAGE, "not attached");
    return RE_OK;
}

void re_dbg_session_init(re_dbg_session_t *s) {
    memset(s, 0, sizeof(*s));
    re_arena_init(&s->arena, 64u * 1024u);
    s->backend = re_dbg_backend_create(&s->arena);
    s->bps = (re_dbg_bps_t *)re_arena_calloc(&s->arena, 1, sizeof(*s->bps));
    s->last_err.code = RE_OK;
}

void re_dbg_session_reset(re_dbg_session_t *s) {
    if (s->backend && s->backend->vt && s->backend->vt->detach && s->attached) {
        if (s->bps)
            (void)re_dbg_bp_disarm_all(s->bps, s->backend);
        s->backend->vt->detach(s->backend);
    }
    clear_target(s);
    RE_ERR_OK(&s->last_err);
    re_arena_reset(&s->arena);
    s->backend = re_dbg_backend_create(&s->arena);
    s->bps = (re_dbg_bps_t *)re_arena_calloc(&s->arena, 1, sizeof(*s->bps));
}

void re_dbg_session_free(re_dbg_session_t *s) {
    if (s->backend && s->backend->vt && s->backend->vt->destroy)
        s->backend->vt->destroy(s->backend);
    s->backend = NULL;
    s->bps = NULL;
    re_arena_free(&s->arena);
}

re_dbg_session_t *re_dbg_session_global(void) {
    if (!g_ready) {
        re_dbg_session_init(&g_session);
        g_ready = true;
    }
    return &g_session;
}

re_err_code_t re_dbg_session_launch(re_dbg_session_t *s, const char *path, const char *args) {
    re_err_code_t c = need_backend(s);
    if (c != RE_OK)
        return c;
    if (!path || !path[0])
        return note(s, RE_E_USAGE, "init needs a target path");
    if (!s->backend->vt->launch)
        return note(s, RE_E_UNSUPPORTED, "launch is not available on this backend");
    c = s->backend->vt->launch(s->backend, path, args);
    if (c != RE_OK)
        return note(s, c, "launch failed or is not implemented");
    clear_target(s);
    if (s->bps)
        re_dbg_bps_clear(s->bps);
    s->attached = true;
    copy_path(s, path);
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_attach(re_dbg_session_t *s, uint32_t pid) {
    re_err_code_t c = need_backend(s);
    if (c != RE_OK)
        return c;
    if (pid == 0)
        return note(s, RE_E_USAGE, "attach needs a non-zero pid");
    if (!s->backend->vt->attach)
        return note(s, RE_E_UNSUPPORTED, "attach is not available on this backend");
    c = s->backend->vt->attach(s->backend, pid);
    if (c != RE_OK)
        return note(s, c, "attach failed or is not implemented");
    clear_target(s);
    if (s->bps)
        re_dbg_bps_clear(s->bps);
    s->attached = true;
    s->pid = pid;
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_detach(re_dbg_session_t *s) {
    re_err_code_t c = need_backend(s);
    if (c != RE_OK)
        return c;
    if (s->attached && s->bps)
        (void)re_dbg_bp_disarm_all(s->bps, s->backend);
    if (!s->backend->vt->detach)
        return note(s, RE_E_UNSUPPORTED, "detach is not available on this backend");
    c = s->backend->vt->detach(s->backend);
    if (c != RE_OK)
        return note(s, c, "detach failed or is not implemented");
    clear_target(s);
    if (s->bps)
        re_dbg_bps_clear(s->bps);
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_kill(re_dbg_session_t *s) {
    re_err_code_t c = need_backend(s);
    if (c != RE_OK)
        return c;
    if (!s->backend->vt->kill)
        return note(s, RE_E_UNSUPPORTED, "kill is not available on this backend");
    c = s->backend->vt->kill(s->backend);
    if (c != RE_OK)
        return note(s, c, "kill failed or is not implemented");
    clear_target(s);
    if (s->bps)
        re_dbg_bps_clear(s->bps);
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_cont(re_dbg_session_t *s) {
    re_err_code_t c = need_attached(s);
    if (c != RE_OK)
        return c;
    if (!s->backend->vt->cont)
        return note(s, RE_E_UNSUPPORTED, "continue is not available on this backend");
    if (s->bps) {
        c = re_dbg_bp_prepare_cont(s->bps, s->backend);
        if (c != RE_OK)
            return note(s, c, "continue re-arm step failed");
        if (s->bps->rearm_then_cont)
            return note(s, RE_OK, NULL);
    }
    c = s->backend->vt->cont(s->backend);
    if (c != RE_OK)
        return note(s, c, "continue failed or is not implemented");
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_step(re_dbg_session_t *s) {
    re_err_code_t c = need_attached(s);
    if (c != RE_OK)
        return c;
    if (!s->backend->vt->step)
        return note(s, RE_E_UNSUPPORTED, "step is not available on this backend");
    if (s->bps && s->bps->need_rearm)
        s->bps->rearm_then_cont = false;
    c = s->backend->vt->step(s->backend);
    if (c != RE_OK)
        return note(s, c, "step failed or is not implemented");
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_step_over(re_dbg_session_t *s) {
    re_dbg_regs_t regs;
    uint8_t buf[16];
    size_t len = 0;
    re_err_code_t c = need_attached(s);
    if (c != RE_OK)
        return c;
    if (!s->backend->vt->get_regs || !s->backend->vt->read_mem)
        return note(s, RE_E_UNSUPPORTED, "step-over needs regs and memory");
    c = s->backend->vt->get_regs(s->backend, s->tid, &regs);
    if (c != RE_OK)
        return note(s, c, "step-over get_regs failed");
    memset(buf, 0, sizeof(buf));
    c = s->backend->vt->read_mem(s->backend, regs.rip, buf, sizeof(buf));
    if (c != RE_OK)
        return note(s, c, "step-over read_mem failed");
    if (re_dbg_x86_is_call(buf, sizeof(buf), &len) && s->bps) {
        c = re_dbg_bp_set(s->bps, s->backend, regs.rip + len, true);
        if (c != RE_OK)
            return note(s, c, "step-over temp breakpoint failed");
        s->bps->step_over_active = true;
        return re_dbg_session_cont(s);
    }
    return re_dbg_session_step(s);
}

re_err_code_t re_dbg_session_pause(re_dbg_session_t *s) {
    re_err_code_t c = need_attached(s);
    if (c != RE_OK)
        return c;
    if (!s->backend->vt->pause)
        return note(s, RE_E_UNSUPPORTED, "pause is not available on this backend");
    c = s->backend->vt->pause(s->backend);
    if (c != RE_OK)
        return note(s, c, "pause failed or is not implemented");
    return note(s, RE_OK, NULL);
}

static void post_event(re_dbg_session_t *s, re_dbg_event_t *ev) {
    bool consumed = false;
    if (!s->bps || !ev)
        return;
    if (ev->kind == RE_DBG_EV_BREAK)
        (void)re_dbg_bp_on_hit(s->bps, s->backend, ev->tid, &ev->addr);
    if (ev->kind == RE_DBG_EV_STEP) {
        (void)re_dbg_bp_after_step(s->bps, s->backend, &consumed);
        if (consumed)
            ev->kind = RE_DBG_EV_NONE;
    }
}

re_err_code_t re_dbg_session_wait_event(re_dbg_session_t *s, re_dbg_event_t *ev,
                                        uint32_t timeout_ms) {
    re_dbg_event_t local;
    re_err_code_t c = need_backend(s);
    if (c != RE_OK)
        return c;
    if (!ev)
        ev = &local;
    memset(ev, 0, sizeof(*ev));
    if (!s->backend->vt->wait_event)
        return note(s, RE_E_UNSUPPORTED, "wait_event is not available on this backend");
    for (;;) {
        c = s->backend->vt->wait_event(s->backend, ev, timeout_ms);
        if (c != RE_OK)
            return note(s, c, "wait_event failed or is not implemented");
        post_event(s, ev);
        if (ev->kind != RE_DBG_EV_NONE)
            break;
        if (timeout_ms != 0)
            break;
    }
    s->last_event = *ev;
    if (ev->pid)
        s->pid = ev->pid;
    if (ev->tid)
        s->tid = ev->tid;
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_bp_set(re_dbg_session_t *s, uint64_t addr) {
    re_err_code_t c = need_attached(s);
    if (c != RE_OK)
        return c;
    if (!s->bps)
        return note(s, RE_E_INTERNAL, "breakpoint table missing");
    c = re_dbg_bp_set(s->bps, s->backend, addr, false);
    if (c != RE_OK)
        return note(s, c, "bp set failed");
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_bp_clear(re_dbg_session_t *s, uint64_t addr) {
    re_err_code_t c = need_attached(s);
    if (c != RE_OK)
        return c;
    c = re_dbg_bp_clear(s->bps, s->backend, addr);
    if (c != RE_OK)
        return note(s, c, "bp clear failed");
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_bp_enable(re_dbg_session_t *s, uint64_t addr) {
    re_err_code_t c = need_attached(s);
    if (c != RE_OK)
        return c;
    c = re_dbg_bp_enable(s->bps, s->backend, addr);
    if (c != RE_OK)
        return note(s, c, "bp enable failed");
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_bp_disable(re_dbg_session_t *s, uint64_t addr) {
    re_err_code_t c = need_attached(s);
    if (c != RE_OK)
        return c;
    c = re_dbg_bp_disable(s->bps, s->backend, addr);
    if (c != RE_OK)
        return note(s, c, "bp disable failed");
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_bp_list(re_dbg_session_t *s, re_dbg_bp_info_t *out, size_t cap,
                                     size_t *n_out) {
    re_err_code_t c = need_backend(s);
    if (c != RE_OK)
        return c;
    if (!s->bps)
        return note(s, RE_E_INTERNAL, "breakpoint table missing");
    c = re_dbg_bp_list(s->bps, out, cap, n_out);
    if (c != RE_OK)
        return note(s, c, "bp list failed");
    return note(s, RE_OK, NULL);
}

const char *re_dbg_event_kind_str(re_dbg_event_kind_t kind) {
    switch (kind) {
        case RE_DBG_EV_NONE:
            return "none";
        case RE_DBG_EV_CREATE:
            return "create";
        case RE_DBG_EV_EXIT:
            return "exit";
        case RE_DBG_EV_BREAK:
            return "break";
        case RE_DBG_EV_STEP:
            return "step";
        case RE_DBG_EV_EXCEPTION:
            return "exception";
        case RE_DBG_EV_LOAD_MODULE:
            return "load_module";
        case RE_DBG_EV_UNLOAD_MODULE:
            return "unload_module";
        case RE_DBG_EV_THREAD:
            return "thread";
        default:
            return "unknown";
    }
}

const char *re_dbg_backend_name(void) {
#if defined(_WIN32)
    return "windows-debug";
#elif defined(__linux__)
    return "linux-ptrace";
#else
    return "stub";
#endif
}
