// re_dbg_linux_evt.c - wait_event, continue, step, and register helpers.
// Module: feature (C11).
// Owns: the ptrace wait loop, pause, and register get/set helpers.
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
        re_dbg_linux_q_push(st, RE_DBG_EV_THREAD_CREATE, (pid_t)new_tid, 0, 1);
        int st2 = 0;
        pid_t w = waitpid((pid_t)new_tid, &st2, __WALL | WNOHANG);
        if (w == (pid_t)new_tid && WIFSTOPPED(st2))
            (void)ptrace(PTRACE_CONT, (pid_t)new_tid, NULL, NULL);
    }
    st->stopped = true;
    st->cur_tid = tid;
    re_dbg_linux_q_push(st, RE_DBG_EV_BREAKPOINT, tid, re_dbg_linux_rip(tid), SIGTRAP);
}

static void handle_exit_event(re_dbg_linux_t *st, pid_t tid) {
    unsigned long code = 0;
    (void)ptrace(PTRACE_GETEVENTMSG, tid, NULL, &code);
    if (tid == st->pid) {
        /* Collapse PTRACE_EVENT_EXIT + WIFEXITED into one EXIT: resume so the
         * real reaping wait delivers a single event (avoids double-exit races). */
        st->exiting = true;
        st->exit_code = (int32_t)code;
        (void)ptrace(PTRACE_CONT, tid, NULL, NULL);
        st->stopped = false;
        return;
    }
    re_dbg_linux_tid_del(st, tid);
    re_dbg_linux_q_push(st, RE_DBG_EV_THREAD_EXIT, tid, 0, 0);
    (void)ptrace(PTRACE_CONT, tid, NULL, NULL);
    st->stopped = false;
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
        re_dbg_linux_q_push(st, RE_DBG_EV_BREAKPOINT, tid, rip, SIGTRAP);
    }
    st->stopped = true;
    st->cur_tid = tid;
}

static void push_exception(re_dbg_linux_t *st, pid_t tid, int32_t code) {
    re_dbg_event_t *slot;
    if (!re_dbg_linux_q_push(st, RE_DBG_EV_EXCEPTION, tid, re_dbg_linux_rip(tid), code))
        return;
    /* q_push leaves first_chance false; Linux signal stops are first-chance. */
    if (st->q_n > 0) {
        slot = &st->q[st->q_n - 1];
        slot->first_chance = true;
    }
}

static void decode_stop(re_dbg_linux_t *st, pid_t tid, int status) {
    int sig = WSTOPSIG(status);
    st->last_status = status;
    if (sig == SIGTRAP) {
        decode_sigtrap(st, tid, status);
        return;
    }
    if (sig == SIGSTOP) {
        re_dbg_linux_q_push(st, RE_DBG_EV_BREAKPOINT, tid, re_dbg_linux_rip(tid), SIGSTOP);
        st->stopped = true;
        st->cur_tid = tid;
        return;
    }
    push_exception(st, tid, sig);
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
            if (w == st->pid) {
                if (st->exiting)
                    code = st->exit_code;
                re_dbg_linux_q_push(st, RE_DBG_EV_EXIT, w, 0, code);
                st->attached = false;
                st->exiting = false;
                st->exit_code = code;
                st->stopped = false;
            } else {
                /* Non-main tid reaped after PTRACE_EVENT_EXIT + CONT. */
                re_dbg_linux_q_push(st, RE_DBG_EV_THREAD_EXIT, w, 0, 0);
            }
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
    long sig;
    if (!st)
        return RE_E_INTERNAL;
    if (!st->attached)
        return re_dbg_linux_fail(st, RE_E_USAGE, "inferior exited");
    pid_t tid = st->cur_tid > 0 ? st->cur_tid : st->pid;
    sig = st->pass_sig;
    st->pass_sig = 0;
    if (ptrace(PTRACE_CONT, tid, NULL, (void *)(uintptr_t)(unsigned long)sig) != 0)
        return re_dbg_linux_fail_errno(st, "PTRACE_CONT");
    st->stopped = false;
    st->stepping = false;
    return RE_OK;
}

re_err_code_t re_dbg_linux_set_pass(re_dbg_backend_t *b, bool pass, int32_t code) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    if (!st)
        return RE_E_INTERNAL;
    if (!st->attached)
        return re_dbg_linux_fail(st, RE_E_USAGE, "not attached");
    st->pass_sig = pass ? (int)code : 0;
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
    if (tid)
        st->cur_tid = t; /* thread switch focuses this tid for cont/step */
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
        out[i].suspended = st->susp[i];
    }
    return RE_OK;
}

#    include <stddef.h>

static pid_t hw_tid(re_dbg_linux_t *st) {
    return st->cur_tid > 0 ? st->cur_tid : st->pid;
}

static unsigned long dr_off(int idx) {
    return (unsigned long)offsetof(struct user, u_debugreg[idx]);
}

static uint32_t dr_len_bits(uint8_t size) {
    if (size == 2u)
        return 1u;
    if (size == 8u)
        return 2u;
    if (size == 4u)
        return 3u;
    return 0u;
}

static re_err_code_t peek_dr(re_dbg_linux_t *st, pid_t tid, int idx, uint64_t *out) {
    errno = 0;
    long v = ptrace(PTRACE_PEEKUSER, tid, (void *)(uintptr_t)dr_off(idx), NULL);
    if (errno != 0)
        return re_dbg_linux_fail_errno(st, "PTRACE_PEEKUSER debugreg");
    if (out)
        *out = (uint64_t)v;
    return RE_OK;
}

static re_err_code_t poke_dr(re_dbg_linux_t *st, pid_t tid, int idx, uint64_t val) {
    if (ptrace(PTRACE_POKEUSER, tid, (void *)(uintptr_t)dr_off(idx), (void *)(uintptr_t)val) != 0)
        return re_dbg_linux_fail_errno(st, "PTRACE_POKEUSER debugreg");
    return RE_OK;
}

re_err_code_t re_dbg_linux_hw_bp_set(re_dbg_backend_t *b, int slot, uint64_t addr, uint8_t size,
                                     uint8_t access) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    uint64_t dr7 = 0;
    uint32_t shift;
    pid_t tid;
    re_err_code_t c;
    if (!st)
        return RE_E_INTERNAL;
    if (!st->attached)
        return re_dbg_linux_fail(st, RE_E_USAGE, "not attached");
    if (slot < 0 || slot > 3)
        return re_dbg_linux_fail(st, RE_E_RANGE, "hw slot must be 0..3");
    if (access != 0u && access != 1u && access != 3u)
        return re_dbg_linux_fail(st, RE_E_USAGE, "bad hw access");
    tid = hw_tid(st);
    c = poke_dr(st, tid, slot, addr);
    if (c != RE_OK)
        return c;
    c = peek_dr(st, tid, 7, &dr7);
    if (c != RE_OK)
        return c;
    dr7 |= (1ull << (unsigned)(slot * 2)); /* local enable */
    shift = 16u + (uint32_t)slot * 4u;
    dr7 &= ~(0xfull << shift);
    dr7 |= ((uint64_t)access & 3u) << shift;
    dr7 |= ((uint64_t)dr_len_bits(size) & 3u) << (shift + 2u);
    return poke_dr(st, tid, 7, dr7);
}

re_err_code_t re_dbg_linux_hw_bp_clear(re_dbg_backend_t *b, int slot) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    uint64_t dr7 = 0;
    uint32_t shift;
    pid_t tid;
    re_err_code_t c;
    if (!st)
        return RE_E_INTERNAL;
    if (!st->attached)
        return re_dbg_linux_fail(st, RE_E_USAGE, "not attached");
    if (slot < 0 || slot > 3)
        return re_dbg_linux_fail(st, RE_E_RANGE, "hw slot must be 0..3");
    tid = hw_tid(st);
    c = peek_dr(st, tid, 7, &dr7);
    if (c != RE_OK)
        return c;
    dr7 &= ~(3ull << (unsigned)(slot * 2));
    shift = 16u + (uint32_t)slot * 4u;
    dr7 &= ~(0xfull << shift);
    c = poke_dr(st, tid, 7, dr7);
    if (c != RE_OK)
        return c;
    return poke_dr(st, tid, slot, 0);
}

re_err_code_t re_dbg_linux_hw_bp_hit(re_dbg_backend_t *b, uint32_t tid, int *slot_out,
                                     uint64_t *addr_out) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    uint64_t dr6 = 0;
    pid_t t;
    int i;
    re_err_code_t c;
    if (slot_out)
        *slot_out = -1;
    if (addr_out)
        *addr_out = 0;
    if (!st)
        return RE_E_INTERNAL;
    if (!st->attached)
        return re_dbg_linux_fail(st, RE_E_USAGE, "not attached");
    t = tid ? (pid_t)tid : hw_tid(st);
    c = peek_dr(st, t, 6, &dr6);
    if (c != RE_OK)
        return c;
    for (i = 0; i < 4; i++) {
        if ((dr6 & (1ull << (unsigned)i)) == 0)
            continue;
        if (slot_out)
            *slot_out = i;
        if (addr_out)
            (void)peek_dr(st, t, i, addr_out);
        (void)poke_dr(st, t, 6, 0);
        return RE_OK;
    }
    return RE_E_NOTFOUND;
}

#else
typedef int re_dbg_linux_evt_placeholder;
#endif /* __linux__ */
