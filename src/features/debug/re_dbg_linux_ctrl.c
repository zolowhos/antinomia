// re_dbg_linux_ctrl.c - launch, attach, detach, and kill for Linux ptrace.
// Module: feature (C11).
// Owns: process lifetime under ptrace (TRACEME/SEIZE) with clear Yama errors.
// Depends: re_dbg_linux_priv.h. Leaves the target stopped at entry after launch.
#define _GNU_SOURCE
#include "features/debug/re_dbg_linux_priv.h"

#if defined(__linux__)

#    include <stdio.h>
#    include <string.h>

extern char **environ;

static re_err_code_t wait_stop(re_dbg_linux_t *st, pid_t tid, int *status_out) {
    int status = 0;
    for (;;) {
        pid_t w = waitpid(tid, &status, __WALL);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return re_dbg_linux_fail_errno(st, "waitpid");
        }
        if (WIFSTOPPED(status)) {
            if (status_out)
                *status_out = status;
            st->cur_tid = w;
            st->last_status = status;
            st->stopped = true;
            return RE_OK;
        }
        if (WIFEXITED(status) || WIFSIGNALED(status))
            return re_dbg_linux_fail(st, RE_E_IO, "target exited before stop");
    }
}

static void queue_create_break(re_dbg_linux_t *st, pid_t tid) {
    uint64_t rip = re_dbg_linux_rip(tid);
    re_dbg_linux_q_push(st, RE_DBG_EV_CREATE, tid, rip, 0);
    re_dbg_linux_q_push(st, RE_DBG_EV_BREAKPOINT, tid, rip, SIGTRAP);
}

static size_t copy_cstr(char *dst, size_t cap, const char *src) {
    size_t n = 0;
    if (!dst || cap == 0)
        return 0;
    if (!src) {
        dst[0] = 0;
        return 0;
    }
    while (src[n] && n + 1 < cap) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = 0;
    return n;
}

static int build_argv(char *buf, size_t buf_n, char **argv, int argv_cap, const char *path,
                      const char *args) {
    size_t o = 0;
    int n = 0;
    if (argv_cap < 2)
        return -1;
    o += copy_cstr(buf + o, buf_n - o, path) + 1;
    if (o >= buf_n)
        return -1;
    argv[n++] = buf;
    if (!args || !args[0]) {
        argv[n] = NULL;
        return n;
    }
    char *ap = buf + o;
    size_t an = copy_cstr(ap, buf_n - o, args);
    if (an + 1 > buf_n - o)
        return -1;
    size_t i = 0;
    while (i <= an && n + 1 < argv_cap) {
        while (i < an && (ap[i] == ' ' || ap[i] == '\t'))
            i++;
        if (i >= an)
            break;
        argv[n++] = ap + i;
        while (i < an && ap[i] != ' ' && ap[i] != '\t')
            i++;
        if (i < an)
            ap[i++] = 0;
    }
    argv[n] = NULL;
    return n;
}

static void child_exec(const char *path, const char *args) {
    char buf[4096];
    char *argv[64];
    if (build_argv(buf, sizeof(buf), argv, 64, path, args) < 1)
        _exit(127);
    if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) != 0)
        _exit(127);
    raise(SIGSTOP);
    execve(path, argv, environ);
    _exit(127);
}

re_err_code_t re_dbg_linux_launch(re_dbg_backend_t *b, const char *path, const char *args) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    if (!st)
        return RE_E_INTERNAL;
    if (st->attached)
        return re_dbg_linux_fail(st, RE_E_USAGE, "already attached; detach or kill first");
    if (!path || !path[0])
        return re_dbg_linux_fail(st, RE_E_USAGE, "launch needs a path");
    pid_t child = fork();
    if (child < 0)
        return re_dbg_linux_fail_errno(st, "fork");
    if (child == 0)
        child_exec(path, args);
    int status = 0;
    re_err_code_t rc = wait_stop(st, child, &status);
    if (rc != RE_OK) {
        kill(child, SIGKILL);
        (void)waitpid(child, NULL, __WALL);
        return rc;
    }
    if (WSTOPSIG(status) != SIGSTOP) {
        kill(child, SIGKILL);
        (void)waitpid(child, NULL, __WALL);
        return re_dbg_linux_fail(st, RE_E_IO, "child did not stop before exec");
    }
    rc = re_dbg_linux_set_opts(st, child);
    if (rc != RE_OK) {
        kill(child, SIGKILL);
        (void)waitpid(child, NULL, __WALL);
        return rc;
    }
    if (ptrace(PTRACE_CONT, child, NULL, NULL) != 0) {
        kill(child, SIGKILL);
        (void)waitpid(child, NULL, __WALL);
        return re_dbg_linux_fail_errno(st, "PTRACE_CONT");
    }
    rc = wait_stop(st, child, &status);
    if (rc != RE_OK) {
        kill(child, SIGKILL);
        (void)waitpid(child, NULL, __WALL);
        return rc;
    }
    st->pid = child;
    st->cur_tid = child;
    st->attached = true;
    st->our_child = true;
    st->stopped = true;
    re_dbg_linux_tid_add(st, child);
    queue_create_break(st, child);
    RE_ERR_OK(&st->last_err);
    return RE_OK;
}

static re_err_code_t seize_or_attach(re_dbg_linux_t *st, pid_t pid) {
    if (ptrace(PTRACE_SEIZE, pid, NULL, (void *)(uintptr_t)RE_DBG_LINUX_OPTS) == 0) {
        if (ptrace(PTRACE_INTERRUPT, pid, NULL, NULL) != 0)
            return re_dbg_linux_fail_errno(st, "PTRACE_INTERRUPT");
        return RE_OK;
    }
    if (errno != EPERM && errno != EACCES && errno != EINVAL)
        return re_dbg_linux_fail_errno(st, "PTRACE_SEIZE");
    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) != 0)
        return re_dbg_linux_fail_errno(st, "PTRACE_ATTACH");
    return re_dbg_linux_set_opts(st, pid);
}

re_err_code_t re_dbg_linux_attach(re_dbg_backend_t *b, uint32_t pid) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    if (!st)
        return RE_E_INTERNAL;
    if (st->attached)
        return re_dbg_linux_fail(st, RE_E_USAGE, "already attached; detach or kill first");
    if (pid == 0)
        return re_dbg_linux_fail(st, RE_E_USAGE, "attach needs a nonzero pid");
    re_err_code_t rc = seize_or_attach(st, (pid_t)pid);
    if (rc != RE_OK)
        return rc;
    int status = 0;
    rc = wait_stop(st, (pid_t)pid, &status);
    if (rc != RE_OK) {
        (void)ptrace(PTRACE_DETACH, (pid_t)pid, NULL, NULL);
        return rc;
    }
    st->pid = (pid_t)pid;
    st->cur_tid = (pid_t)pid;
    st->attached = true;
    st->our_child = false;
    st->stopped = true;
    re_dbg_linux_tid_add(st, (pid_t)pid);
    queue_create_break(st, (pid_t)pid);
    RE_ERR_OK(&st->last_err);
    return RE_OK;
}

re_err_code_t re_dbg_linux_detach(re_dbg_backend_t *b) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    if (!st)
        return RE_E_INTERNAL;
    if (!st->attached)
        return RE_E_USAGE;
    pid_t pid = st->pid;
    if (ptrace(PTRACE_DETACH, st->cur_tid > 0 ? st->cur_tid : pid, NULL, NULL) != 0) {
        if (errno != ESRCH && ptrace(PTRACE_DETACH, pid, NULL, NULL) != 0 && errno != ESRCH)
            return re_dbg_linux_fail_errno(st, "PTRACE_DETACH");
    }
    re_dbg_linux_clear(st);
    return RE_OK;
}

re_err_code_t re_dbg_linux_kill(re_dbg_backend_t *b) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    if (!st)
        return RE_E_INTERNAL;
    if (!st->attached)
        return RE_E_USAGE;
    pid_t pid = st->pid;
    bool child = st->our_child;
    if (kill(pid, SIGKILL) != 0 && errno != ESRCH)
        return re_dbg_linux_fail_errno(st, "kill");
    if (child) {
        int status = 0;
        while (waitpid(pid, &status, __WALL) < 0 && errno == EINTR) {
        }
    } else {
        (void)ptrace(PTRACE_DETACH, pid, NULL, NULL);
    }
    re_dbg_linux_clear(st);
    return RE_OK;
}

#else
typedef int re_dbg_linux_ctrl_placeholder;
#endif /* __linux__ */
