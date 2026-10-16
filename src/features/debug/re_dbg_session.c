// re_dbg_session.c - long-lived debug session arena and control API.
// Module: feature (C11).
// Owns: session init/reset/free, global handle, and session-to-backend wiring.
// Depends: re_dbg.h, re_disasm. Session arena is not the per-command arena.
#include "features/debug/re_dbg.h"

#include <string.h>

#define RE_DBG_DISASM_WINDOW 256u

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

void re_dbg_session_init(re_dbg_session_t *s) {
    memset(s, 0, sizeof(*s));
    re_arena_init(&s->arena, 64u * 1024u);
    s->backend = re_dbg_backend_create(&s->arena);
    s->last_err.code = RE_OK;
    re_disasm_cache_clear(&s->disasm_cache);
}

void re_dbg_session_reset(re_dbg_session_t *s) {
    if (s->backend && s->backend->vt && s->backend->vt->detach && s->attached)
        s->backend->vt->detach(s->backend);
    clear_target(s);
    RE_ERR_OK(&s->last_err);
    re_disasm_cache_clear(&s->disasm_cache);
    re_arena_reset(&s->arena);
    s->backend = re_dbg_backend_create(&s->arena);
}

void re_dbg_session_free(re_dbg_session_t *s) {
    if (s->backend && s->backend->vt && s->backend->vt->destroy)
        s->backend->vt->destroy(s->backend);
    s->backend = NULL;
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
    re_disasm_cache_clear(&s->disasm_cache);
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
    re_disasm_cache_clear(&s->disasm_cache);
    s->attached = true;
    s->pid = pid;
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_detach(re_dbg_session_t *s) {
    re_err_code_t c = need_backend(s);
    if (c != RE_OK)
        return c;
    if (!s->backend->vt->detach)
        return note(s, RE_E_UNSUPPORTED, "detach is not available on this backend");
    c = s->backend->vt->detach(s->backend);
    if (c != RE_OK)
        return note(s, c, "detach failed or is not implemented");
    clear_target(s);
    re_disasm_cache_clear(&s->disasm_cache);
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
    re_disasm_cache_clear(&s->disasm_cache);
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_cont(re_dbg_session_t *s) {
    re_err_code_t c = need_backend(s);
    if (c != RE_OK)
        return c;
    if (!s->backend->vt->cont)
        return note(s, RE_E_UNSUPPORTED, "continue is not available on this backend");
    c = s->backend->vt->cont(s->backend);
    if (c != RE_OK)
        return note(s, c, "continue failed or is not implemented");
    return note(s, RE_OK, NULL);
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
    c = s->backend->vt->wait_event(s->backend, ev, timeout_ms);
    if (c != RE_OK)
        return note(s, c, "wait_event failed or is not implemented");
    s->last_event = *ev;
    if (ev->pid)
        s->pid = ev->pid;
    if (ev->tid)
        s->tid = ev->tid;
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_get_regs(re_dbg_session_t *s, uint32_t tid, re_dbg_regs_t *out) {
    re_err_code_t c = need_backend(s);
    uint32_t use = tid ? tid : s->tid;
    if (c != RE_OK)
        return c;
    if (!out)
        return note(s, RE_E_USAGE, "get_regs needs an output buffer");
    if (!s->backend->vt->get_regs)
        return note(s, RE_E_UNSUPPORTED, "get_regs is not available on this backend");
    c = s->backend->vt->get_regs(s->backend, use, out);
    if (c != RE_OK)
        return note(s, c, "get_regs failed or is not implemented");
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_set_regs(re_dbg_session_t *s, uint32_t tid, const re_dbg_regs_t *in) {
    re_err_code_t c = need_backend(s);
    uint32_t use = tid ? tid : s->tid;
    if (c != RE_OK)
        return c;
    if (!in)
        return note(s, RE_E_USAGE, "set_regs needs register values");
    if (!s->backend->vt->set_regs)
        return note(s, RE_E_UNSUPPORTED, "set_regs is not available on this backend");
    c = s->backend->vt->set_regs(s->backend, use, in);
    if (c != RE_OK)
        return note(s, c, "set_regs failed or is not implemented");
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_read_mem(re_dbg_session_t *s, uint64_t addr, void *dst, size_t n) {
    re_err_code_t c = need_backend(s);
    if (c != RE_OK)
        return c;
    if (!s->backend->vt->read_mem)
        return note(s, RE_E_UNSUPPORTED, "read_mem is not available on this backend");
    c = s->backend->vt->read_mem(s->backend, addr, dst, n);
    if (c != RE_OK)
        return note(s, c, "read_mem failed or is not implemented");
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_write_mem(re_dbg_session_t *s, uint64_t addr, const void *src,
                                       size_t n) {
    re_err_code_t c = need_backend(s);
    if (c != RE_OK)
        return c;
    if (!s->backend->vt->write_mem)
        return note(s, RE_E_UNSUPPORTED, "write_mem is not available on this backend");
    c = s->backend->vt->write_mem(s->backend, addr, src, n);
    if (c != RE_OK)
        return note(s, c, "write_mem failed or is not implemented");
    re_disasm_cache_invalidate(&s->disasm_cache, addr, n);
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_disasm(re_dbg_session_t *s, uint64_t addr, size_t count,
                                    re_disasm_insn_t *out, size_t *n_out) {
    uint8_t win[RE_DBG_DISASM_WINDOW];
    size_t want = RE_DBG_DISASM_WINDOW;
    re_err_code_t c;
    size_t i;
    if (n_out)
        *n_out = 0;
    c = need_backend(s);
    if (c != RE_OK)
        return c;
    if (!out || count == 0)
        return note(s, RE_E_USAGE, "disasm needs an output buffer");
    if (count > RE_DISASM_CACHE_N)
        count = RE_DISASM_CACHE_N;
    memset(win, 0, sizeof(win));
    c = re_dbg_session_read_mem(s, addr, win, want);
    if (c != RE_OK)
        return c;
    c = re_disasm_cache_fill(&s->disasm_cache, addr, win, want, count);
    if (c != RE_OK)
        return note(s, c, "disasm decode failed");
    for (i = 0; i < s->disasm_cache.n && i < count; i++)
        out[i] = s->disasm_cache.lines[i];
    if (n_out)
        *n_out = i;
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
    return "windows";
#elif defined(__linux__)
    return "linux";
#else
    return "stub";
#endif
}
