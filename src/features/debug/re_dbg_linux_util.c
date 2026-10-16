// re_dbg_linux_util.c - shared helpers for the Linux ptrace backend.
// Module: feature (C11).
// Owns: error mapping, tid set, event queue, and RIP peek helpers.
// Depends: re_dbg_linux_priv.h. No malloc; arena-backed state lives in the impl.
#define _GNU_SOURCE
#include "features/debug/re_dbg_linux_priv.h"

#if defined(__linux__)

#    include "utils/sys/re_log.h"

#    include <stdio.h>
#    include <string.h>
#    include <unistd.h>
#    ifndef SYS_tgkill
#        include <sys/syscall.h>
#    endif

static int linux_tgkill(pid_t tgid, pid_t tid, int sig) {
    return (int)syscall(SYS_tgkill, tgid, tid, sig);
}

re_err_code_t re_dbg_linux_fail(re_dbg_linux_t *st, re_err_code_t code, const char *msg) {
    if (st)
        RE_ERR_SET(&st->last_err, code, msg ? msg : "");
    if (msg && msg[0])
        RE_LOGE("%s", msg);
    return code;
}

re_err_code_t re_dbg_linux_fail_errno(re_dbg_linux_t *st, const char *what) {
    char buf[RE_ERR_MSG_MAX];
    int e = errno;
    if (e == EPERM || e == EACCES) {
        snprintf(buf, sizeof(buf),
                 "%s denied (errno %d); check Yama ptrace_scope in "
                 "/proc/sys/kernel/yama/ptrace_scope",
                 what ? what : "ptrace", e);
        return re_dbg_linux_fail(st, RE_E_PRIVILEGE, buf);
    }
    snprintf(buf, sizeof(buf), "%s failed: errno %d", what ? what : "ptrace", e);
    return re_dbg_linux_fail(st, RE_E_IO, buf);
}

re_err_code_t re_dbg_linux_set_opts(re_dbg_linux_t *st, pid_t tid) {
    if (ptrace(PTRACE_SETOPTIONS, tid, NULL, (void *)(uintptr_t)RE_DBG_LINUX_OPTS) != 0)
        return re_dbg_linux_fail_errno(st, "PTRACE_SETOPTIONS");
    return RE_OK;
}

void re_dbg_linux_clear(re_dbg_linux_t *st) {
    if (!st)
        return;
    memset(st, 0, sizeof(*st));
    RE_ERR_OK(&st->last_err);
}

bool re_dbg_linux_tid_add(re_dbg_linux_t *st, pid_t tid) {
    if (!st || tid <= 0)
        return false;
    for (size_t i = 0; i < st->n_tids; i++) {
        if (st->tids[i] == tid)
            return true;
    }
    if (st->n_tids >= RE_DBG_LINUX_MAX_TIDS)
        return false;
    st->tids[st->n_tids] = tid;
    st->susp[st->n_tids] = false;
    st->n_tids++;
    return true;
}

void re_dbg_linux_tid_del(re_dbg_linux_t *st, pid_t tid) {
    if (!st)
        return;
    for (size_t i = 0; i < st->n_tids; i++) {
        if (st->tids[i] != tid)
            continue;
        st->tids[i] = st->tids[st->n_tids - 1];
        st->susp[i] = st->susp[st->n_tids - 1];
        st->n_tids--;
        return;
    }
}

static int linux_tid_index(re_dbg_linux_t *st, pid_t tid) {
    size_t i;
    for (i = 0; i < st->n_tids; i++) {
        if (st->tids[i] == tid)
            return (int)i;
    }
    return -1;
}

re_err_code_t re_dbg_linux_thread_suspend(re_dbg_backend_t *b, uint32_t tid) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    int idx;
    pid_t t;
    if (!st)
        return RE_E_INTERNAL;
    if (!st->attached)
        return re_dbg_linux_fail(st, RE_E_USAGE, "not attached");
    t = tid ? (pid_t)tid : (st->cur_tid > 0 ? st->cur_tid : st->pid);
    idx = linux_tid_index(st, t);
    if (idx < 0)
        return re_dbg_linux_fail(st, RE_E_NOTFOUND, "thread not found");
    if (st->susp[idx])
        return RE_OK;
    /* Mark suspended; at a stop this is a soft freeze until resume. */
    st->susp[idx] = true;
    if (!st->stopped && t != st->cur_tid) {
        if (linux_tgkill(st->pid, t, SIGSTOP) != 0)
            return re_dbg_linux_fail_errno(st, "tgkill SIGSTOP");
    }
    return RE_OK;
}

re_err_code_t re_dbg_linux_thread_resume(re_dbg_backend_t *b, uint32_t tid) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    int idx;
    pid_t t;
    if (!st)
        return RE_E_INTERNAL;
    if (!st->attached)
        return re_dbg_linux_fail(st, RE_E_USAGE, "not attached");
    t = tid ? (pid_t)tid : (st->cur_tid > 0 ? st->cur_tid : st->pid);
    idx = linux_tid_index(st, t);
    if (idx < 0)
        return re_dbg_linux_fail(st, RE_E_NOTFOUND, "thread not found");
    if (!st->susp[idx])
        return RE_OK;
    st->susp[idx] = false;
    if (!st->stopped) {
        if (ptrace(PTRACE_CONT, t, NULL, NULL) != 0)
            return re_dbg_linux_fail_errno(st, "PTRACE_CONT resume");
    }
    return RE_OK;
}

uint64_t re_dbg_linux_rip(pid_t tid) {
    struct user_regs_struct regs;
    memset(&regs, 0, sizeof(regs));
    if (ptrace(PTRACE_GETREGS, tid, NULL, &regs) != 0)
        return 0;
    return (uint64_t)regs.rip;
}

bool re_dbg_linux_q_push(re_dbg_linux_t *st, re_dbg_event_kind_t kind, pid_t tid, uint64_t addr,
                         int32_t code) {
    if (!st || st->q_n >= RE_DBG_LINUX_MAX_EV)
        return false;
    re_dbg_event_t *e = &st->q[st->q_n++];
    memset(e, 0, sizeof(*e));
    e->kind = kind;
    e->pid = (uint32_t)st->pid;
    e->tid = (uint32_t)tid;
    e->addr = addr;
    e->code = code;
    return true;
}

bool re_dbg_linux_q_pop(re_dbg_linux_t *st, re_dbg_event_t *out) {
    if (!st || st->q_n == 0)
        return false;
    if (out)
        *out = st->q[0];
    for (size_t i = 1; i < st->q_n; i++)
        st->q[i - 1] = st->q[i];
    st->q_n--;
    return true;
}

#else
/* Compiled on non-Linux hosts so the feature glob still links. */
typedef int re_dbg_linux_util_placeholder;
#endif /* __linux__ */
