// re_dbg_win_wait.c - WaitForDebugEvent mapping and continue for Windows backend.
// Module: feature (C11).
// Owns: event translation, thread tracking, pump-to-initial-breakpoint, cont.
// Depends: re_dbg_win_priv.h. Compiled on all hosts; body is Windows-only.
#include "features/debug/re_dbg.h"

#include <string.h>

#if !defined(_WIN32)

/* Lifecycle unit provides the full stub vtable on non-Windows hosts. */
typedef int re_dbg_win_wait_stub_t;

#else /* _WIN32 */

#    include "features/debug/re_dbg_win_priv.h"

void re_dbg_win_enable_debug_priv(void) {
    HANDLE tok = NULL;
    TOKEN_PRIVILEGES tp;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok))
        return;
    memset(&tp, 0, sizeof(tp));
    if (LookupPrivilegeValueA(NULL, "SeDebugPrivilege", &tp.Privileges[0].Luid)) {
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(tok, FALSE, &tp, 0, NULL, NULL);
    }
    CloseHandle(tok);
}

static void win_thr_clear(re_dbg_win_impl_t *w) {
    for (size_t i = 0; i < w->n_thr; i++) {
        if (w->thr[i].handle)
            CloseHandle(w->thr[i].handle);
        w->thr[i].handle = NULL;
        w->thr[i].tid = 0;
    }
    w->n_thr = 0;
}

static void win_thr_add(re_dbg_win_impl_t *w, DWORD tid, HANDLE h) {
    for (size_t i = 0; i < w->n_thr; i++) {
        if (w->thr[i].tid == tid) {
            if (w->thr[i].handle && w->thr[i].handle != h)
                CloseHandle(w->thr[i].handle);
            w->thr[i].handle = h;
            return;
        }
    }
    if (w->n_thr >= RE_DBG_WIN_MAX_THR) {
        if (h)
            CloseHandle(h);
        return;
    }
    w->thr[w->n_thr].tid = tid;
    w->thr[w->n_thr].handle = h;
    w->thr[w->n_thr].suspend_depth = 0;
    w->n_thr++;
}

static void win_thr_del(re_dbg_win_impl_t *w, DWORD tid) {
    for (size_t i = 0; i < w->n_thr; i++) {
        if (w->thr[i].tid != tid)
            continue;
        if (w->thr[i].handle)
            CloseHandle(w->thr[i].handle);
        w->thr[i] = w->thr[w->n_thr - 1];
        w->n_thr--;
        return;
    }
}

void re_dbg_win_reset_state(re_dbg_win_impl_t *w) {
    win_thr_clear(w);
    re_dbg_win_mod_clear(w);
    if (w->process) {
        CloseHandle(w->process);
        w->process = NULL;
    }
    w->pid = 0;
    w->last_pid = 0;
    w->last_tid = 0;
    w->cont_status = DBG_CONTINUE;
    w->active = false;
    w->pending = false;
    w->stashed = false;
    w->saw_exit = false;
    memset(&w->stash, 0, sizeof(w->stash));
}

static void win_fill_base(re_dbg_event_t *ev, const DEBUG_EVENT *de) {
    memset(ev, 0, sizeof(*ev));
    ev->pid = (uint32_t)de->dwProcessId;
    ev->tid = (uint32_t)de->dwThreadId;
}

static void win_map_exception(re_dbg_event_t *ev, const DEBUG_EVENT *de) {
    DWORD code = de->u.Exception.ExceptionRecord.ExceptionCode;
    ev->addr = (uint64_t)(uintptr_t)de->u.Exception.ExceptionRecord.ExceptionAddress;
    ev->code = (int32_t)code;
    ev->first_chance = (de->u.Exception.dwFirstChance != 0);
    if (code == EXCEPTION_BREAKPOINT)
        ev->kind = RE_DBG_EV_BREAKPOINT;
    else if (code == EXCEPTION_SINGLE_STEP)
        ev->kind = RE_DBG_EV_STEP;
    else
        ev->kind = RE_DBG_EV_EXCEPTION;
}

bool re_dbg_win_map_event(re_dbg_event_t *ev, const DEBUG_EVENT *de) {
    win_fill_base(ev, de);
    switch (de->dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            ev->kind = RE_DBG_EV_CREATE;
            ev->addr = (uint64_t)(uintptr_t)de->u.CreateProcessInfo.lpStartAddress;
            return true;
        case EXIT_PROCESS_DEBUG_EVENT:
            ev->kind = RE_DBG_EV_EXIT;
            ev->code = (int32_t)de->u.ExitProcess.dwExitCode;
            return true;
        case CREATE_THREAD_DEBUG_EVENT:
            ev->kind = RE_DBG_EV_THREAD_CREATE;
            ev->code = 1;
            ev->addr = (uint64_t)(uintptr_t)de->u.CreateThread.lpStartAddress;
            return true;
        case EXIT_THREAD_DEBUG_EVENT:
            ev->kind = RE_DBG_EV_THREAD_EXIT;
            ev->code = 0;
            return true;
        case EXCEPTION_DEBUG_EVENT:
            win_map_exception(ev, de);
            return true;
        case LOAD_DLL_DEBUG_EVENT:
            ev->kind = RE_DBG_EV_LOAD_MODULE;
            ev->addr = (uint64_t)(uintptr_t)de->u.LoadDll.lpBaseOfDll;
            return true;
        case UNLOAD_DLL_DEBUG_EVENT:
            ev->kind = RE_DBG_EV_UNLOAD_MODULE;
            ev->addr = (uint64_t)(uintptr_t)de->u.UnloadDll.lpBaseOfDll;
            return true;
        case OUTPUT_DEBUG_STRING_EVENT:
            ev->kind = RE_DBG_EV_OUTPUT;
            ev->addr = (uint64_t)(uintptr_t)de->u.DebugString.lpDebugStringData;
            ev->code = (int32_t)de->u.DebugString.nDebugStringLength;
            return true;
        default:
            return false;
    }
}

void re_dbg_win_track(re_dbg_win_impl_t *w, const DEBUG_EVENT *de) {
    if (de->dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
        const CREATE_PROCESS_DEBUG_INFO *i = &de->u.CreateProcessInfo;
        if (w->process)
            CloseHandle(w->process);
        w->process = i->hProcess;
        w->pid = de->dwProcessId;
        win_thr_add(w, de->dwThreadId, i->hThread);
        re_dbg_win_mod_note(w, (uint64_t)(uintptr_t)i->lpBaseOfImage, i->hFile, i->lpImageName,
                            i->fUnicode != 0);
        if (i->hFile)
            CloseHandle(i->hFile);
        return;
    }
    if (de->dwDebugEventCode == CREATE_THREAD_DEBUG_EVENT) {
        win_thr_add(w, de->dwThreadId, de->u.CreateThread.hThread);
        return;
    }
    if (de->dwDebugEventCode == EXIT_THREAD_DEBUG_EVENT) {
        win_thr_del(w, de->dwThreadId);
        return;
    }
    if (de->dwDebugEventCode == LOAD_DLL_DEBUG_EVENT) {
        const LOAD_DLL_DEBUG_INFO *i = &de->u.LoadDll;
        re_dbg_win_mod_note(w, (uint64_t)(uintptr_t)i->lpBaseOfDll, i->hFile, i->lpImageName,
                            i->fUnicode != 0);
        if (i->hFile)
            CloseHandle(i->hFile);
        return;
    }
    if (de->dwDebugEventCode == UNLOAD_DLL_DEBUG_EVENT)
        re_dbg_win_mod_unload(w, (uint64_t)(uintptr_t)de->u.UnloadDll.lpBaseOfDll);
}

bool re_dbg_win_continue(re_dbg_win_impl_t *w, DWORD status) {
    if (!ContinueDebugEvent(w->last_pid, w->last_tid, status))
        return false;
    w->pending = false;
    return true;
}

re_err_code_t re_dbg_win_pump_to_break(re_dbg_win_impl_t *w) {
    for (;;) {
        DEBUG_EVENT de;
        DWORD st = DBG_CONTINUE;
        if (!WaitForDebugEvent(&de, INFINITE))
            return RE_E_IO;
        w->last_pid = de.dwProcessId;
        w->last_tid = de.dwThreadId;
        re_dbg_win_track(w, &de);
        if (de.dwDebugEventCode == EXCEPTION_DEBUG_EVENT &&
            de.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT) {
            re_dbg_win_map_event(&w->stash, &de);
            w->stashed = true;
            w->pending = true;
            w->cont_status = DBG_CONTINUE;
            return RE_OK;
        }
        if (de.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
            re_dbg_win_map_event(&w->stash, &de);
            w->stashed = true;
            w->pending = true;
            w->saw_exit = true;
            w->cont_status = DBG_CONTINUE;
            return RE_E_IO;
        }
        if (de.dwDebugEventCode == EXCEPTION_DEBUG_EVENT)
            st = DBG_EXCEPTION_NOT_HANDLED;
        if (!ContinueDebugEvent(de.dwProcessId, de.dwThreadId, st))
            return RE_E_IO;
    }
}

re_err_code_t re_dbg_win_wait(re_dbg_backend_t *b, re_dbg_event_t *ev, uint32_t timeout_ms) {
    re_dbg_win_impl_t *w = re_dbg_win_impl(b);
    DWORD start;
    if (!w || !ev)
        return RE_E_USAGE;
    memset(ev, 0, sizeof(*ev));
    if (!w->active)
        return RE_E_USAGE;
    if (w->stashed) {
        *ev = w->stash;
        w->stashed = false;
        return RE_OK;
    }
    if (w->pending)
        return RE_E_USAGE;
    start = GetTickCount();
    for (;;) {
        DEBUG_EVENT de;
        DWORD wait_ms = timeout_ms;
        if (timeout_ms != 0 && timeout_ms != INFINITE) {
            DWORD elapsed = GetTickCount() - start;
            if (elapsed >= timeout_ms)
                return RE_OK;
            wait_ms = timeout_ms - elapsed;
        }
        if (!WaitForDebugEvent(&de, wait_ms)) {
            if (GetLastError() == ERROR_SEM_TIMEOUT)
                return RE_OK;
            return RE_E_IO;
        }
        w->last_pid = de.dwProcessId;
        w->last_tid = de.dwThreadId;
        re_dbg_win_track(w, &de);
        if (!re_dbg_win_map_event(ev, &de)) {
            if (!ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE))
                return RE_E_IO;
            if (timeout_ms == 0)
                return RE_OK;
            continue;
        }
        w->pending = true;
        /* Default: pass exceptions; session/set_pass may override before cont. */
        w->cont_status =
            (ev->kind == RE_DBG_EV_EXCEPTION) ? DBG_EXCEPTION_NOT_HANDLED : DBG_CONTINUE;
        if (ev->kind == RE_DBG_EV_EXIT)
            w->saw_exit = true;
        return RE_OK;
    }
}

re_err_code_t re_dbg_win_set_pass(re_dbg_backend_t *b, bool pass, int32_t code) {
    re_dbg_win_impl_t *w = re_dbg_win_impl(b);
    (void)code;
    if (!w || !w->active)
        return RE_E_USAGE;
    w->cont_status = pass ? DBG_EXCEPTION_NOT_HANDLED : DBG_CONTINUE;
    return RE_OK;
}

re_err_code_t re_dbg_win_cont(re_dbg_backend_t *b) {
    re_dbg_win_impl_t *w = re_dbg_win_impl(b);
    if (!w || !w->pending)
        return RE_E_USAGE;
    if (!re_dbg_win_continue(w, w->cont_status))
        return RE_E_IO;
    w->stashed = false;
    if (w->saw_exit)
        re_dbg_win_reset_state(w);
    return RE_OK;
}

HANDLE re_dbg_win_thread_handle(re_dbg_win_impl_t *w, DWORD tid) {
    size_t i;
    if (!w)
        return NULL;
    if (tid == 0)
        tid = w->last_tid;
    for (i = 0; i < w->n_thr; i++) {
        if (w->thr[i].tid == tid)
            return w->thr[i].handle;
    }
    return NULL;
}

re_err_code_t re_dbg_win_step(re_dbg_backend_t *b) {
    re_dbg_win_impl_t *w = re_dbg_win_impl(b);
    re_dbg_regs_t regs;
    re_err_code_t c;
    if (!w || !w->active || !w->pending)
        return RE_E_USAGE;
    c = re_dbg_win_get_regs(b, (uint32_t)w->last_tid, &regs);
    if (c != RE_OK)
        return c;
    regs.rflags |= 0x100ull; /* TF */
    c = re_dbg_win_set_regs(b, (uint32_t)w->last_tid, &regs);
    if (c != RE_OK)
        return c;
    if (!re_dbg_win_continue(w, w->cont_status))
        return RE_E_IO;
    w->stashed = false;
    return RE_OK;
}

re_err_code_t re_dbg_win_pause(re_dbg_backend_t *b) {
    re_dbg_win_impl_t *w = re_dbg_win_impl(b);
    if (!w || !w->active || !w->process)
        return RE_E_USAGE;
    if (w->pending)
        return RE_OK;
    if (!DebugBreakProcess(w->process))
        return RE_E_IO;
    return RE_OK;
}

re_err_code_t re_dbg_win_enum_threads(re_dbg_backend_t *b, re_dbg_thread_t *out, size_t cap,
                                      size_t *n_out) {
    re_dbg_win_impl_t *w = re_dbg_win_impl(b);
    size_t n;
    if (!w || !w->active)
        return RE_E_USAGE;
    n = w->n_thr;
    if (n_out)
        *n_out = n;
    if (!out || cap == 0)
        return RE_OK;
    if (n > cap)
        n = cap;
    for (size_t i = 0; i < n; i++) {
        memset(&out[i], 0, sizeof(out[i]));
        out[i].tid = (uint32_t)w->thr[i].tid;
        out[i].suspended = w->thr[i].suspend_depth > 0;
    }
    return RE_OK;
}

static re_dbg_win_thr_t *win_thr_find(re_dbg_win_impl_t *w, DWORD tid) {
    size_t i;
    for (i = 0; i < w->n_thr; i++) {
        if (w->thr[i].tid == tid)
            return &w->thr[i];
    }
    return NULL;
}

re_err_code_t re_dbg_win_thread_suspend(re_dbg_backend_t *b, uint32_t tid) {
    re_dbg_win_impl_t *w = re_dbg_win_impl(b);
    re_dbg_win_thr_t *t;
    DWORD use;
    DWORD prev;
    if (!w || !w->active)
        return RE_E_USAGE;
    use = tid ? (DWORD)tid : w->last_tid;
    t = win_thr_find(w, use);
    if (!t || !t->handle)
        return RE_E_NOTFOUND;
    prev = SuspendThread(t->handle);
    if (prev == (DWORD)-1)
        return RE_E_IO;
    t->suspend_depth++;
    return RE_OK;
}

re_err_code_t re_dbg_win_thread_resume(re_dbg_backend_t *b, uint32_t tid) {
    re_dbg_win_impl_t *w = re_dbg_win_impl(b);
    re_dbg_win_thr_t *t;
    DWORD use;
    DWORD prev;
    if (!w || !w->active)
        return RE_E_USAGE;
    use = tid ? (DWORD)tid : w->last_tid;
    t = win_thr_find(w, use);
    if (!t || !t->handle)
        return RE_E_NOTFOUND;
    if (t->suspend_depth <= 0)
        return RE_OK;
    prev = ResumeThread(t->handle);
    if (prev == (DWORD)-1)
        return RE_E_IO;
    t->suspend_depth--;
    return RE_OK;
}

static uint32_t win_prot_bits(DWORD protect) {
    uint32_t p = 0;
    DWORD base = protect & 0xffu;
    if (base == PAGE_READONLY || base == PAGE_READWRITE || base == PAGE_WRITECOPY ||
        base == PAGE_EXECUTE_READ || base == PAGE_EXECUTE_READWRITE ||
        base == PAGE_EXECUTE_WRITECOPY)
        p |= RE_DBG_PROT_R;
    if (base == PAGE_READWRITE || base == PAGE_WRITECOPY || base == PAGE_EXECUTE_READWRITE ||
        base == PAGE_EXECUTE_WRITECOPY)
        p |= RE_DBG_PROT_W;
    if (base == PAGE_EXECUTE || base == PAGE_EXECUTE_READ || base == PAGE_EXECUTE_READWRITE ||
        base == PAGE_EXECUTE_WRITECOPY)
        p |= RE_DBG_PROT_X;
    return p;
}

static re_dbg_map_type_t win_map_type(DWORD type, DWORD protect) {
    (void)protect;
    if (type == MEM_IMAGE)
        return RE_DBG_MAP_IMAGE;
    if (type == MEM_MAPPED)
        return RE_DBG_MAP_MAPPED;
    if (type == MEM_PRIVATE)
        return RE_DBG_MAP_PRIVATE;
    return RE_DBG_MAP_OTHER;
}

re_err_code_t re_dbg_win_enum_maps(re_dbg_backend_t *b, re_dbg_map_t *out, size_t cap,
                                   size_t *n_out) {
    re_dbg_win_impl_t *w = re_dbg_win_impl(b);
    MEMORY_BASIC_INFORMATION mbi;
    uint8_t *addr = NULL;
    size_t n = 0;
    if (n_out)
        *n_out = 0;
    if (!w || !w->active || !w->process)
        return RE_E_USAGE;
    while (VirtualQueryEx(w->process, addr, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT) {
            if (out && n < cap) {
                memset(&out[n], 0, sizeof(out[n]));
                out[n].base = (uint64_t)(uintptr_t)mbi.BaseAddress;
                out[n].size = (uint64_t)mbi.RegionSize;
                out[n].protect = win_prot_bits(mbi.Protect);
                out[n].type = win_map_type(mbi.Type, mbi.Protect);
            }
            n++;
            if (n >= 512u)
                break;
        }
        {
            uint8_t *next = (uint8_t *)mbi.BaseAddress + mbi.RegionSize;
            if (next <= addr)
                break;
            addr = next;
        }
    }
    if (n_out)
        *n_out = n;
    return RE_OK;
}

#endif /* _WIN32 */
