// re_dbg_linux_evt.c - wait_event, continue, step, and register helpers.
// Module: feature (C11).
// Owns: the ptrace wait loop and event decode (create/exit/break/step/thread).
// Depends: re_dbg_linux_priv.h. Breakpoint restore is out of scope for Pack A.
#define _GNU_SOURCE
#include "features/debug/re_dbg_linux_priv.h"

#if defined(__linux__)

#    include <string.h>
#    include <time.h>
#    include <unistd.h>

static void fill_regs(re_dbg_regs_t *out, const struct user_regs_struct *r) {
    out->rax = r->rax;
    out->rbx = r->rbx;
    out->rcx = r->rcx;
    out->rdx = r->rdx;
    out->rsi = r->rsi;
    out->rdi = r->rdi;
    out->rbp = r->rbp;
    out->rsp = r->rsp;
    out->r8 = r->r8;
    out->r9 = r->r9;
    out->r10 = r->r10;
    out->r11 = r->r11;
    out->r12 = r->r12;
    out->r13 = r->r13;
    out->r14 = r->r14;
    out->r15 = r->r15;
    out->rip = r->rip;
    out->rflags = r->eflags;
}

static void apply_regs(struct user_regs_struct *r, const re_dbg_regs_t *in) {
    r->rax = in->rax;
    r->rbx = in->rbx;
    r->rcx = in->rcx;
    r->rdx = in->rdx;
    r->rsi = in->rsi;
    r->rdi = in->rdi;
    r->rbp = in->rbp;
    r->rsp = in->rsp;
    r->r8 = in->r8;
    r->r9 = in->r9;
    r->r10 = in->r10;
    r->r11 = in->r11;
    r->r12 = in->r12;
    r->r13 = in->r13;
    r->r14 = in->r14;
    r->r15 = in->r15;
    r->rip = in->rip;
    r->eflags = in->rflags;
}

static void handle_clone(re_dbg_linux_t *st, pid_t tid) {
    unsigned long new_tid = 0;
    if (ptrace(PTRACE_GETEVENTMSG, tid, NULL, &new_tid) == 0 && new_tid != 0) {
        re_dbg_linux_tid_add(st, (pid_t)new_tid);
        re_dbg_linux_q_push(st, RE_DBG_EV_THREAD, (pid_t)new_tid, 0, 1);
        int st2 = 0;
        pid_t w = waitpid((pid_t)new_tid, &st2, __WALL | WNOHANG);
        if (w == (pid_t)new_tid && WIFSTOPPED(st2))
            (void)ptrace(PTRACE_CONT, (pid_t)new_tid, NULL, NULL);
    }
    st->stopped = true;
    st->cur_tid = tid;
    re_dbg_linux_q_push(st, RE_DBG_EV_BREAK, tid, re_dbg_linux_rip(tid), SIGTRAP);
}

static void handle_exit_event(re_dbg_linux_t *st, pid_t tid) {
    unsigned long code = 0;
    (void)ptrace(PTRACE_GETEVENTMSG, tid, NULL, &code);
    if (tid == st->pid) {
        re_dbg_linux_q_push(st, RE_DBG_EV_EXIT, tid, 0, (int32_t)code);
    } else {
        re_dbg_linux_tid_del(st, tid);
        re_dbg_linux_q_push(st, RE_DBG_EV_THREAD, tid, 0, 0);
        (void)ptrace(PTRACE_CONT, tid, NULL, NULL);
        st->stopped = false;
    }
}

static void decode_sigtrap(re_dbg_linux_t *st, pid_t tid, int status) {
    int ev = status >> 16;
    if (ev == PTRACE_EVENT_CLONE || ev == PTRACE_EVENT_FORK || ev == PTRACE_EVENT_VFORK) {
        handle_clone(st, tid);
        return;
    }
    if (ev == PTRACE_EVENT_EXIT) {
        handle_exit_event(st, tid);
        return;
    }
    if (ev == PTRACE_EVENT_EXEC) {
        re_dbg_linux_q_push(st, RE_DBG_EV_CREATE, tid, re_dbg_linux_rip(tid), 0);
        st->stopped = true;
        st->cur_tid = tid;
        return;
    }
    uint64_t rip = re_dbg_linux_rip(tid);
    if (st->stepping) {
        st->stepping = false;
        re_dbg_linux_q_push(st, RE_DBG_EV_STEP, tid, rip, SIGTRAP);
    } else {
        re_dbg_linux_q_push(st, RE_DBG_EV_BREAK, tid, rip, SIGTRAP);
    }
    st->stopped = true;
    st->cur_tid = tid;
}

static void decode_stop(re_dbg_linux_t *st, pid_t tid, int status) {
    int sig = WSTOPSIG(status);
    st->last_status = status;
    if (sig == SIGTRAP) {
        decode_sigtrap(st, tid, status);
        return;
    }
    if (sig == SIGSTOP) {
        re_dbg_linux_q_push(st, RE_DBG_EV_BREAK, tid, re_dbg_linux_rip(tid), SIGSTOP);
        st->stopped = true;
        st->cur_tid = tid;
        return;
    }
    re_dbg_linux_q_push(st, RE_DBG_EV_EXCEPTION, tid, re_dbg_linux_rip(tid), sig);
    st->stopped = true;
    st->cur_tid = tid;
}

static re_err_code_t wait_one(re_dbg_linux_t *st, uint32_t timeout_ms) {
    int flags = __WALL;
    if (timeout_ms > 0)
        flags |= WNOHANG;
    struct timespec start, now;
    if (timeout_ms > 0)
        clock_gettime(CLOCK_MONOTONIC, &start);
    for (;;) {
        int status = 0;
        pid_t w = waitpid(-1, &status, flags);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            if (errno == ECHILD)
                return re_dbg_linux_fail(st, RE_E_IO, "no traced children");
            return re_dbg_linux_fail_errno(st, "waitpid");
        }
        if (w == 0) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            uint64_t elapsed = (uint64_t)(now.tv_sec - start.tv_sec) * 1000u +
                               (uint64_t)(now.tv_nsec - start.tv_nsec) / 1000000u;
            if (elapsed >= timeout_ms)
                return RE_OK;
            struct timespec ts = {0, 1000000L};
            nanosleep(&ts, NULL);
            continue;
        }
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            int32_t code = WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status);
            re_dbg_linux_q_push(st, RE_DBG_EV_EXIT, w, 0, code);
            if (w == st->pid)
                st->attached = false;
            re_dbg_linux_tid_del(st, w);
            return RE_OK;
        }
        if (WIFSTOPPED(status)) {
            decode_stop(st, w, status);
            return RE_OK;
        }
    }
}

re_err_code_t re_dbg_linux_wait(re_dbg_backend_t *b, re_dbg_event_t *ev, uint32_t timeout_ms) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    if (ev)
        memset(ev, 0, sizeof(*ev));
    if (!st)
        return RE_E_INTERNAL;
    if (!st->attached && st->q_n == 0)
        return re_dbg_linux_fail(st, RE_E_USAGE, "not attached");
    if (st->q_n == 0) {
        re_err_code_t rc = wait_one(st, timeout_ms);
        if (rc != RE_OK)
            return rc;
    }
    if (st->q_n == 0) {
        if (ev)
            ev->kind = RE_DBG_EV_NONE;
        return RE_OK;
    }
    (void)re_dbg_linux_q_pop(st, ev);
    return RE_OK;
}

re_err_code_t re_dbg_linux_cont(re_dbg_backend_t *b) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    if (!st)
        return RE_E_INTERNAL;
    if (!st->attached)
        return re_dbg_linux_fail(st, RE_E_USAGE, "not attached");
    pid_t tid = st->cur_tid > 0 ? st->cur_tid : st->pid;
    if (ptrace(PTRACE_CONT, tid, NULL, NULL) != 0)
        return re_dbg_linux_fail_errno(st, "PTRACE_CONT");
    st->stopped = false;
    st->stepping = false;
    return RE_OK;
}

re_err_code_t re_dbg_linux_step(re_dbg_backend_t *b) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    if (!st)
        return RE_E_INTERNAL;
    if (!st->attached)
        return re_dbg_linux_fail(st, RE_E_USAGE, "not attached");
    pid_t tid = st->cur_tid > 0 ? st->cur_tid : st->pid;
    if (ptrace(PTRACE_SINGLESTEP, tid, NULL, NULL) != 0)
        return re_dbg_linux_fail_errno(st, "PTRACE_SINGLESTEP");
    st->stopped = false;
    st->stepping = true;
    return RE_OK;
}

re_err_code_t re_dbg_linux_pause(re_dbg_backend_t *b) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    pid_t tid;
    if (!st)
        return RE_E_INTERNAL;
    if (!st->attached)
        return re_dbg_linux_fail(st, RE_E_USAGE, "not attached");
    if (st->stopped)
        return RE_OK;
    tid = st->cur_tid > 0 ? st->cur_tid : st->pid;
    if (tgkill(st->pid, tid, SIGSTOP) != 0) {
        if (kill(st->pid, SIGSTOP) != 0)
            return re_dbg_linux_fail_errno(st, "tgkill/SIGSTOP");
    }
    return RE_OK;
}

static re_err_code_t peek_bytes(re_dbg_linux_t *st, pid_t tid, uint64_t addr, void *dst, size_t n) {
    uint8_t *out = (uint8_t *)dst;
    size_t off = 0;
    while (off < n) {
        uint64_t cur = addr + off;
        uint64_t aligned = cur & ~(uint64_t)(sizeof(long) - 1u);
        size_t skip = (size_t)(cur - aligned);
        size_t avail = sizeof(long) - skip;
        size_t chunk = n - off < avail ? n - off : avail;
        long word;
        errno = 0;
        word = ptrace(PTRACE_PEEKDATA, tid, (void *)(uintptr_t)aligned, NULL);
        if (word == -1 && errno != 0)
            return re_dbg_linux_fail_errno(st, "PTRACE_PEEKDATA");
        memcpy(out + off, (uint8_t *)&word + skip, chunk);
        off += chunk;
    }
    return RE_OK;
}

static re_err_code_t poke_bytes(re_dbg_linux_t *st, pid_t tid, uint64_t addr, const void *src,
                                size_t n) {
    const uint8_t *in = (const uint8_t *)src;
    size_t off = 0;
    while (off < n) {
        uint64_t cur = addr + off;
        uint64_t aligned = cur & ~(uint64_t)(sizeof(long) - 1u);
        size_t skip = (size_t)(cur - aligned);
        size_t avail = sizeof(long) - skip;
        size_t chunk = n - off < avail ? n - off : avail;
        long word;
        errno = 0;
        word = ptrace(PTRACE_PEEKDATA, tid, (void *)(uintptr_t)aligned, NULL);
        if (word == -1 && errno != 0)
            return re_dbg_linux_fail_errno(st, "PTRACE_PEEKDATA");
        memcpy((uint8_t *)&word + skip, in + off, chunk);
        if (ptrace(PTRACE_POKEDATA, tid, (void *)(uintptr_t)aligned, (void *)word) != 0)
            return re_dbg_linux_fail_errno(st, "PTRACE_POKEDATA");
        off += chunk;
    }
    return RE_OK;
}

re_err_code_t re_dbg_linux_read(re_dbg_backend_t *b, uint64_t addr, void *dst, size_t n) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    pid_t tid;
    if (dst && n)
        memset(dst, 0, n);
    if (!st)
        return RE_E_INTERNAL;
    if (!st->attached)
        return re_dbg_linux_fail(st, RE_E_USAGE, "not attached");
    if (!dst && n)
        return RE_E_USAGE;
    if (n == 0)
        return RE_OK;
    tid = st->cur_tid > 0 ? st->cur_tid : st->pid;
    return peek_bytes(st, tid, addr, dst, n);
}

re_err_code_t re_dbg_linux_write(re_dbg_backend_t *b, uint64_t addr, const void *src, size_t n) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    pid_t tid;
    if (!st)
        return RE_E_INTERNAL;
    if (!st->attached)
        return re_dbg_linux_fail(st, RE_E_USAGE, "not attached");
    if ((!src && n) || n == 0)
        return n == 0 ? RE_OK : RE_E_USAGE;
    tid = st->cur_tid > 0 ? st->cur_tid : st->pid;
    return poke_bytes(st, tid, addr, src, n);
}

re_err_code_t re_dbg_linux_get_regs(re_dbg_backend_t *b, uint32_t tid, re_dbg_regs_t *out) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    if (out)
        memset(out, 0, sizeof(*out));
    if (!st)
        return RE_E_INTERNAL;
    if (!st->attached)
        return re_dbg_linux_fail(st, RE_E_USAGE, "not attached");
    if (!out)
        return RE_E_USAGE;
    pid_t t = tid ? (pid_t)tid : (st->cur_tid > 0 ? st->cur_tid : st->pid);
    struct user_regs_struct r;
    memset(&r, 0, sizeof(r));
    if (ptrace(PTRACE_GETREGS, t, NULL, &r) != 0)
        return re_dbg_linux_fail_errno(st, "PTRACE_GETREGS");
    fill_regs(out, &r);
    return RE_OK;
}

re_err_code_t re_dbg_linux_set_regs(re_dbg_backend_t *b, uint32_t tid, const re_dbg_regs_t *in) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    if (!st)
        return RE_E_INTERNAL;
    if (!st->attached)
        return re_dbg_linux_fail(st, RE_E_USAGE, "not attached");
    if (!in)
        return RE_E_USAGE;
    pid_t t = tid ? (pid_t)tid : (st->cur_tid > 0 ? st->cur_tid : st->pid);
    struct user_regs_struct r;
    memset(&r, 0, sizeof(r));
    if (ptrace(PTRACE_GETREGS, t, NULL, &r) != 0)
        return re_dbg_linux_fail_errno(st, "PTRACE_GETREGS");
    apply_regs(&r, in);
    if (ptrace(PTRACE_SETREGS, t, NULL, &r) != 0)
        return re_dbg_linux_fail_errno(st, "PTRACE_SETREGS");
    return RE_OK;
}

re_err_code_t re_dbg_linux_enum_threads(re_dbg_backend_t *b, re_dbg_thread_t *out, size_t cap,
                                        size_t *n_out) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    if (n_out)
        *n_out = 0;
    if (!st)
        return RE_E_INTERNAL;
    if (!st->attached)
        return re_dbg_linux_fail(st, RE_E_USAGE, "not attached");
    size_t n = st->n_tids;
    if (n_out)
        *n_out = n;
    if (!out || cap == 0)
        return RE_OK;
    size_t lim = n < cap ? n : cap;
    for (size_t i = 0; i < lim; i++) {
        memset(&out[i], 0, sizeof(out[i]));
        out[i].tid = (uint32_t)st->tids[i];
        out[i].rip = re_dbg_linux_rip(st->tids[i]);
    }
    return RE_OK;
}

#else
typedef int re_dbg_linux_evt_placeholder;
#endif /* __linux__ */
