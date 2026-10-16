// re_dbg_linux_mem.c - process memory read/write for the Linux ptrace backend.
// Module: feature (C11).
// Owns: process_vm_readv/writev with ptrace peek/poke fallback.
// Depends: re_dbg_linux_priv.h. Invalidation of disasm cache is session-side.
#define _GNU_SOURCE
#include "features/debug/re_dbg_linux_priv.h"

#if defined(__linux__)

#    include <string.h>
#    include <sys/uio.h>

static re_err_code_t peek_bytes(re_dbg_linux_t *st, pid_t tid, uint64_t addr, void *dst, size_t n) {
    uint8_t *out = (uint8_t *)dst;
    size_t i = 0;
    while (i < n) {
        errno = 0;
        long word = ptrace(PTRACE_PEEKDATA, tid, (void *)(uintptr_t)(addr + i), NULL);
        if (errno != 0)
            return re_dbg_linux_fail_errno(st, "PTRACE_PEEKDATA");
        size_t chunk = sizeof(long);
        if (chunk > n - i)
            chunk = n - i;
        memcpy(out + i, &word, chunk);
        i += chunk;
    }
    return RE_OK;
}

static re_err_code_t poke_bytes(re_dbg_linux_t *st, pid_t tid, uint64_t addr, const void *src,
                                size_t n) {
    const uint8_t *in = (const uint8_t *)src;
    size_t i = 0;
    while (i < n) {
        uint64_t a = addr + i;
        size_t off = (size_t)(a % sizeof(long));
        uint64_t aligned = a - off;
        errno = 0;
        long word = ptrace(PTRACE_PEEKDATA, tid, (void *)(uintptr_t)aligned, NULL);
        if (errno != 0)
            return re_dbg_linux_fail_errno(st, "PTRACE_PEEKDATA");
        uint8_t tmp[sizeof(long)];
        memcpy(tmp, &word, sizeof(long));
        size_t chunk = sizeof(long) - off;
        if (chunk > n - i)
            chunk = n - i;
        memcpy(tmp + off, in + i, chunk);
        memcpy(&word, tmp, sizeof(long));
        if (ptrace(PTRACE_POKEDATA, tid, (void *)(uintptr_t)aligned, (void *)word) != 0)
            return re_dbg_linux_fail_errno(st, "PTRACE_POKEDATA");
        i += chunk;
    }
    return RE_OK;
}

static re_err_code_t vm_read(re_dbg_linux_t *st, uint64_t addr, void *dst, size_t n) {
    struct iovec local;
    struct iovec remote;
    ssize_t got;
    local.iov_base = dst;
    local.iov_len = n;
    remote.iov_base = (void *)(uintptr_t)addr;
    remote.iov_len = n;
    got = process_vm_readv(st->pid, &local, 1, &remote, 1, 0);
    if (got < 0)
        return RE_E_IO;
    if ((size_t)got != n)
        return re_dbg_linux_fail(st, RE_E_IO, "short process_vm_readv");
    return RE_OK;
}

static re_err_code_t vm_write(re_dbg_linux_t *st, uint64_t addr, const void *src, size_t n) {
    struct iovec local;
    struct iovec remote;
    ssize_t got;
    local.iov_base = (void *)(uintptr_t)src;
    local.iov_len = n;
    remote.iov_base = (void *)(uintptr_t)addr;
    remote.iov_len = n;
    got = process_vm_writev(st->pid, &local, 1, &remote, 1, 0);
    if (got < 0)
        return RE_E_IO;
    if ((size_t)got != n)
        return re_dbg_linux_fail(st, RE_E_IO, "short process_vm_writev");
    return RE_OK;
}

re_err_code_t re_dbg_linux_read_mem(re_dbg_backend_t *b, uint64_t addr, void *dst, size_t n) {
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
    if (vm_read(st, addr, dst, n) == RE_OK)
        return RE_OK;
    tid = st->cur_tid > 0 ? st->cur_tid : st->pid;
    return peek_bytes(st, tid, addr, dst, n);
}

re_err_code_t re_dbg_linux_write_mem(re_dbg_backend_t *b, uint64_t addr, const void *src,
                                     size_t n) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    pid_t tid;
    if (!st)
        return RE_E_INTERNAL;
    if (!st->attached)
        return re_dbg_linux_fail(st, RE_E_USAGE, "not attached");
    if ((!src && n) || n == 0)
        return n == 0 ? RE_OK : RE_E_USAGE;
    if (vm_write(st, addr, src, n) == RE_OK)
        return RE_OK;
    tid = st->cur_tid > 0 ? st->cur_tid : st->pid;
    return poke_bytes(st, tid, addr, src, n);
}

#else
typedef int re_dbg_linux_mem_placeholder;
#endif /* __linux__ */
