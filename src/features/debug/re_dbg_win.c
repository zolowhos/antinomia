// re_dbg_win.c - Windows Debug API backend (launch, attach, detach, kill).
// Module: feature (C11).
// Owns: CreateProcess/DebugActiveProcess lifecycle and the Windows vtable.
// Depends: re_dbg.h, re_dbg_win_priv.h on Windows. Non-Windows builds keep a stub.
#include "features/debug/re_dbg.h"

#include <string.h>

#if !defined(_WIN32)

static re_err_code_t stub_launch(re_dbg_backend_t *b, const char *path, const char *args) {
    (void)b;
    (void)path;
    (void)args;
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_attach(re_dbg_backend_t *b, uint32_t pid) {
    (void)b;
    (void)pid;
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_detach(re_dbg_backend_t *b) {
    (void)b;
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_kill(re_dbg_backend_t *b) {
    (void)b;
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_wait(re_dbg_backend_t *b, re_dbg_event_t *ev, uint32_t timeout_ms) {
    (void)b;
    (void)timeout_ms;
    if (ev)
        memset(ev, 0, sizeof(*ev));
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_cont(re_dbg_backend_t *b) {
    (void)b;
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_step(re_dbg_backend_t *b) {
    (void)b;
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_get_regs(re_dbg_backend_t *b, uint32_t tid, re_dbg_regs_t *out) {
    (void)b;
    (void)tid;
    if (out)
        memset(out, 0, sizeof(*out));
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_set_regs(re_dbg_backend_t *b, uint32_t tid, const re_dbg_regs_t *in) {
    (void)b;
    (void)tid;
    (void)in;
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_read(re_dbg_backend_t *b, uint64_t addr, void *dst, size_t n) {
    (void)b;
    (void)addr;
    if (dst && n)
        memset(dst, 0, n);
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_write(re_dbg_backend_t *b, uint64_t addr, const void *src, size_t n) {
    (void)b;
    (void)addr;
    (void)src;
    (void)n;
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_threads(re_dbg_backend_t *b, re_dbg_thread_t *out, size_t cap,
                                  size_t *n_out) {
    (void)b;
    (void)out;
    (void)cap;
    if (n_out)
        *n_out = 0;
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_modules(re_dbg_backend_t *b, re_dbg_module_t *out, size_t cap,
                                  size_t *n_out) {
    (void)b;
    (void)out;
    (void)cap;
    if (n_out)
        *n_out = 0;
    return RE_E_UNSUPPORTED;
}
static void stub_destroy(re_dbg_backend_t *b) {
    (void)b;
}
static const re_dbg_backend_vtbl_t kVtbl = {stub_launch,   stub_attach, stub_detach, stub_kill,
                                            stub_wait,     stub_cont,   stub_step,   stub_get_regs,
                                            stub_set_regs, stub_read,   stub_write,  stub_threads,
                                            stub_modules,  stub_destroy};

re_dbg_backend_t *re_dbg_backend_win_create(re_arena_t *a) {
    re_dbg_backend_t *b = (re_dbg_backend_t *)re_arena_alloc(a, sizeof(*b));
    if (!b)
        return NULL;
    b->vt = &kVtbl;
    b->impl = NULL;
    return b;
}

#else /* _WIN32 */

#    include "features/debug/re_dbg_win_priv.h"

static re_err_code_t win_build_cmd(char *dst, size_t cap, const char *path, const char *args) {
    size_t pi = 0;
    size_t di = 0;
    if (!path || !path[0] || cap < 4)
        return RE_E_USAGE;
    dst[di++] = '"';
    while (path[pi] && di + 2 < cap)
        dst[di++] = path[pi++];
    if (path[pi])
        return RE_E_RANGE;
    dst[di++] = '"';
    if (args && args[0]) {
        if (di + 1 >= cap)
            return RE_E_RANGE;
        dst[di++] = ' ';
        size_t ai = 0;
        while (args[ai] && di + 1 < cap)
            dst[di++] = args[ai++];
        if (args[ai])
            return RE_E_RANGE;
    }
    dst[di] = 0;
    return RE_OK;
}

static re_err_code_t win_launch(re_dbg_backend_t *b, const char *path, const char *args) {
    re_dbg_win_impl_t *w = re_dbg_win_impl(b);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmd[32768];
    re_err_code_t rc;
    if (!w)
        return RE_E_INTERNAL;
    if (w->active)
        return RE_E_USAGE;
    rc = win_build_cmd(cmd, sizeof(cmd), path, args);
    if (rc != RE_OK)
        return rc;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    re_dbg_win_enable_debug_priv();
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_CONSOLE,
                        NULL, NULL, &si, &pi))
        return RE_E_IO;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    w->active = true;
    w->pid = pi.dwProcessId;
    DebugSetProcessKillOnExit(FALSE);
    rc = re_dbg_win_pump_to_break(w);
    if (rc != RE_OK) {
        re_dbg_win_reset_state(w);
        return rc;
    }
    return RE_OK;
}

static re_err_code_t win_attach(re_dbg_backend_t *b, uint32_t pid) {
    re_dbg_win_impl_t *w = re_dbg_win_impl(b);
    re_err_code_t rc;
    if (!w)
        return RE_E_INTERNAL;
    if (w->active || pid == 0)
        return RE_E_USAGE;
    re_dbg_win_enable_debug_priv();
    if (!DebugActiveProcess((DWORD)pid))
        return RE_E_IO;
    DebugSetProcessKillOnExit(FALSE);
    w->active = true;
    w->pid = (DWORD)pid;
    rc = re_dbg_win_pump_to_break(w);
    if (rc != RE_OK) {
        DebugActiveProcessStop((DWORD)pid);
        re_dbg_win_reset_state(w);
        return rc;
    }
    return RE_OK;
}

static re_err_code_t win_detach(re_dbg_backend_t *b) {
    re_dbg_win_impl_t *w = re_dbg_win_impl(b);
    DWORD pid;
    if (!w || !w->active)
        return RE_E_USAGE;
    pid = w->pid;
    DebugSetProcessKillOnExit(FALSE);
    if (w->pending && !re_dbg_win_continue(w, DBG_CONTINUE))
        return RE_E_IO;
    w->stashed = false;
    if (!DebugActiveProcessStop(pid))
        return RE_E_IO;
    re_dbg_win_reset_state(w);
    return RE_OK;
}

static re_err_code_t win_kill(re_dbg_backend_t *b) {
    re_dbg_win_impl_t *w = re_dbg_win_impl(b);
    if (!w || !w->active)
        return RE_E_USAGE;
    DebugSetProcessKillOnExit(TRUE);
    if (w->process)
        TerminateProcess(w->process, 1);
    if (w->pending)
        re_dbg_win_continue(w, DBG_CONTINUE);
    w->stashed = false;
    for (;;) {
        DEBUG_EVENT de;
        if (!WaitForDebugEvent(&de, 5000))
            break;
        w->last_pid = de.dwProcessId;
        w->last_tid = de.dwThreadId;
        re_dbg_win_track(w, &de);
        ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
        if (de.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT)
            break;
    }
    re_dbg_win_reset_state(w);
    return RE_OK;
}

static re_err_code_t win_step(re_dbg_backend_t *b) {
    (void)b;
    return RE_E_UNSUPPORTED;
}
static re_err_code_t win_modules(re_dbg_backend_t *b, re_dbg_module_t *out, size_t cap,
                                 size_t *n_out) {
    (void)b;
    (void)out;
    (void)cap;
    if (n_out)
        *n_out = 0;
    return RE_E_UNSUPPORTED;
}

static void win_destroy(re_dbg_backend_t *b) {
    re_dbg_win_impl_t *w = re_dbg_win_impl(b);
    if (!w)
        return;
    if (w->active)
        (void)win_kill(b);
    re_dbg_win_reset_state(w);
}

static const re_dbg_backend_vtbl_t kVtbl = {win_launch,
                                            win_attach,
                                            win_detach,
                                            win_kill,
                                            re_dbg_win_wait,
                                            re_dbg_win_cont,
                                            win_step,
                                            re_dbg_win_get_regs,
                                            re_dbg_win_set_regs,
                                            re_dbg_win_read_mem,
                                            re_dbg_win_write_mem,
                                            re_dbg_win_enum_threads,
                                            win_modules,
                                            win_destroy};

re_dbg_backend_t *re_dbg_backend_win_create(re_arena_t *a) {
    re_dbg_backend_t *b = (re_dbg_backend_t *)re_arena_alloc(a, sizeof(*b));
    re_dbg_win_impl_t *w;
    if (!b)
        return NULL;
    w = (re_dbg_win_impl_t *)re_arena_calloc(a, 1, sizeof(*w));
    if (!w)
        return NULL;
    b->vt = &kVtbl;
    b->impl = w;
    return b;
}

#endif /* _WIN32 */
