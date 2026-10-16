// re_dbg_linux_priv.h - shared state for the Linux ptrace backend.
// Module: feature (C11).
// Owns: the linux impl struct and helpers shared by the linux .c units.
// Depends: re_dbg.h. Linux only; not part of the public API surface.
#pragma once

#if defined(__linux__)

#    ifndef _GNU_SOURCE
#        define _GNU_SOURCE
#    endif

#    include "features/debug/re_dbg.h"

#    include <errno.h>
#    include <signal.h>
#    include <stdbool.h>
#    include <stdint.h>
#    include <unistd.h>
#    include <sys/ptrace.h>
#    include <sys/types.h>
#    include <sys/user.h>
#    include <sys/wait.h>

#    define RE_DBG_LINUX_MAX_TIDS 64
#    define RE_DBG_LINUX_MAX_EV 8
#    define RE_DBG_LINUX_OPTS                                                                 \
        (PTRACE_O_TRACECLONE | PTRACE_O_TRACEEXEC | PTRACE_O_TRACEEXIT | PTRACE_O_TRACEFORK | \
         PTRACE_O_TRACEVFORK)

typedef struct {
    pid_t pid;
    pid_t cur_tid;
    bool attached;
    bool our_child;
    bool stopped;
    bool stepping;
    int last_status;
    int pass_sig; /* signal delivered on next PTRACE_CONT; 0 = swallow */
    re_err_t last_err;
    pid_t tids[RE_DBG_LINUX_MAX_TIDS];
    size_t n_tids;
    re_dbg_event_t q[RE_DBG_LINUX_MAX_EV];
    size_t q_n;
} re_dbg_linux_t;

static inline re_dbg_linux_t *re_dbg_linux(re_dbg_backend_t *b) {
    return b ? (re_dbg_linux_t *)b->impl : NULL;
}

re_err_code_t re_dbg_linux_fail(re_dbg_linux_t *st, re_err_code_t code, const char *msg);
re_err_code_t re_dbg_linux_fail_errno(re_dbg_linux_t *st, const char *what);
re_err_code_t re_dbg_linux_set_opts(re_dbg_linux_t *st, pid_t tid);
void re_dbg_linux_clear(re_dbg_linux_t *st);
bool re_dbg_linux_tid_add(re_dbg_linux_t *st, pid_t tid);
void re_dbg_linux_tid_del(re_dbg_linux_t *st, pid_t tid);
uint64_t re_dbg_linux_rip(pid_t tid);
bool re_dbg_linux_q_push(re_dbg_linux_t *st, re_dbg_event_kind_t kind, pid_t tid, uint64_t addr,
                         int32_t code);
bool re_dbg_linux_q_pop(re_dbg_linux_t *st, re_dbg_event_t *out);

re_err_code_t re_dbg_linux_launch(re_dbg_backend_t *b, const char *path, const char *args);
re_err_code_t re_dbg_linux_attach(re_dbg_backend_t *b, uint32_t pid);
re_err_code_t re_dbg_linux_detach(re_dbg_backend_t *b);
re_err_code_t re_dbg_linux_kill(re_dbg_backend_t *b);
re_err_code_t re_dbg_linux_wait(re_dbg_backend_t *b, re_dbg_event_t *ev, uint32_t timeout_ms);
re_err_code_t re_dbg_linux_cont(re_dbg_backend_t *b);
re_err_code_t re_dbg_linux_step(re_dbg_backend_t *b);
re_err_code_t re_dbg_linux_pause(re_dbg_backend_t *b);
re_err_code_t re_dbg_linux_get_regs(re_dbg_backend_t *b, uint32_t tid, re_dbg_regs_t *out);
re_err_code_t re_dbg_linux_set_regs(re_dbg_backend_t *b, uint32_t tid, const re_dbg_regs_t *in);
re_err_code_t re_dbg_linux_read_mem(re_dbg_backend_t *b, uint64_t addr, void *dst, size_t n);
re_err_code_t re_dbg_linux_write_mem(re_dbg_backend_t *b, uint64_t addr, const void *src, size_t n);
re_err_code_t re_dbg_linux_enum_threads(re_dbg_backend_t *b, re_dbg_thread_t *out, size_t cap,
                                        size_t *n_out);
re_err_code_t re_dbg_linux_enum_modules(re_dbg_backend_t *b, re_dbg_module_t *out, size_t cap,
                                        size_t *n_out);
re_err_code_t re_dbg_linux_set_pass(re_dbg_backend_t *b, bool pass, int32_t code);

#endif /* __linux__ */
