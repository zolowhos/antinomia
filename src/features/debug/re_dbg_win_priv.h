// re_dbg_win_priv.h - shared Windows backend state for process-control units.
// Module: feature (C11).
// Owns: impl layout and helpers shared by win lifecycle and wait-event files.
// Depends: re_dbg.h, windows.h. Included only from Windows backend .c files.
#pragma once

#include "features/debug/re_dbg.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define RE_DBG_WIN_MAX_THR 64

typedef struct {
    DWORD tid;
    HANDLE handle;
} re_dbg_win_thr_t;

typedef struct {
    HANDLE process;
    DWORD pid;
    DWORD last_pid;
    DWORD last_tid;
    DWORD cont_status;
    bool active;
    bool pending;
    bool stashed;
    bool saw_exit;
    re_dbg_event_t stash;
    re_dbg_win_thr_t thr[RE_DBG_WIN_MAX_THR];
    size_t n_thr;
} re_dbg_win_impl_t;

static inline re_dbg_win_impl_t *re_dbg_win_impl(re_dbg_backend_t *b) {
    return b ? (re_dbg_win_impl_t *)b->impl : NULL;
}

void re_dbg_win_enable_debug_priv(void);
void re_dbg_win_reset_state(re_dbg_win_impl_t *w);
void re_dbg_win_track(re_dbg_win_impl_t *w, const DEBUG_EVENT *de);
bool re_dbg_win_map_event(re_dbg_event_t *ev, const DEBUG_EVENT *de);
bool re_dbg_win_continue(re_dbg_win_impl_t *w, DWORD status);
re_err_code_t re_dbg_win_pump_to_break(re_dbg_win_impl_t *w);
re_err_code_t re_dbg_win_wait(re_dbg_backend_t *b, re_dbg_event_t *ev, uint32_t timeout_ms);
re_err_code_t re_dbg_win_cont(re_dbg_backend_t *b);
re_err_code_t re_dbg_win_step(re_dbg_backend_t *b);
re_err_code_t re_dbg_win_pause(re_dbg_backend_t *b);
re_err_code_t re_dbg_win_get_regs(re_dbg_backend_t *b, uint32_t tid, re_dbg_regs_t *out);
re_err_code_t re_dbg_win_set_regs(re_dbg_backend_t *b, uint32_t tid, const re_dbg_regs_t *in);
re_err_code_t re_dbg_win_read_mem(re_dbg_backend_t *b, uint64_t addr, void *dst, size_t n);
re_err_code_t re_dbg_win_write_mem(re_dbg_backend_t *b, uint64_t addr, const void *src, size_t n);
re_err_code_t re_dbg_win_enum_threads(re_dbg_backend_t *b, re_dbg_thread_t *out, size_t cap,
                                      size_t *n_out);
HANDLE re_dbg_win_thread_handle(re_dbg_win_impl_t *w, DWORD tid);
