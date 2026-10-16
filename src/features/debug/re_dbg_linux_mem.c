// re_dbg_linux_mem.c - process memory read/write for the Linux ptrace backend.
// Module: feature (C11).
// Owns: process_vm/ptrace mem R/W and enum_modules from /proc maps.
// Depends: re_dbg_linux_priv.h. Invalidation of disasm cache is session-side.
#define _GNU_SOURCE
#include "features/debug/re_dbg_linux_priv.h"

#if defined(__linux__)

#    include <fcntl.h>
#    include <stdio.h>
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





#    define RE_DBG_LINUX_MAX_MOD 128

typedef struct {
    uint64_t base;
    uint64_t end;
    char path[260];
} linux_mod_t;

static size_t copy_path(char *dst, size_t cap, const char *src, size_t n) {
    size_t i = 0;
    if (!dst || cap == 0)
        return 0;
    while (i + 1 < cap && i < n && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
    return i;
}

static bool path_is_file(const char *p) {
    return p && p[0] == '/';
}

static int find_mod(linux_mod_t *mods, size_t n, const char *path) {
    for (size_t i = 0; i < n; i++) {
        if (re_str_eq_cstr(re_str(mods[i].path), path))
            return (int)i;
    }
    return -1;
}

static bool parse_map_line(const char *line, uint64_t *lo, uint64_t *hi, char *path,
                           size_t path_cap) {
    unsigned long long a = 0, b = 0;
    char perms[8];
    unsigned long long off = 0;
    char dev[16];
    unsigned long long inode = 0;
    const char *p;
    int n;
    path[0] = 0;
    n = sscanf(line, "%llx-%llx %7s %llx %15s %llu", &a, &b, perms, &off, dev, &inode);
    if (n < 6)
        return false;
    *lo = (uint64_t)a;
    *hi = (uint64_t)b;
    p = line;
    /* Skip address, perms, offset, device, inode; remainder is the path. */
    for (int i = 0; i < 5; i++) {
        while (*p && *p != ' ')
            p++;
        while (*p == ' ')
            p++;
    }
    if (*p) {
        size_t plen = 0;
        while (p[plen] && p[plen] != '\n' && p[plen] != '\r')
            plen++;
        copy_path(path, path_cap, p, plen);
    }
    return true;
}


static bool elf_magic_ok(re_dbg_backend_t *b, uint64_t base) {
    uint8_t magic[4];
    if (re_dbg_linux_read_mem(b, base, magic, sizeof(magic)) != RE_OK)
        return true; /* keep the map entry even if peek fails */
    return magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
}

static void merge_map(linux_mod_t *mods, size_t *n_mod, uint64_t lo, uint64_t hi,
                      const char *path) {
    int idx = find_mod(mods, *n_mod, path);
    re_str_t ps = re_str(path);
    if (idx < 0) {
        if (*n_mod >= RE_DBG_LINUX_MAX_MOD)
            return;
        memset(&mods[*n_mod], 0, sizeof(mods[*n_mod]));
        mods[*n_mod].base = lo;
        mods[*n_mod].end = hi;
        copy_path(mods[*n_mod].path, sizeof(mods[*n_mod].path), path, ps.n);
        (*n_mod)++;
        return;
    }
    if (lo < mods[idx].base)
        mods[idx].base = lo;
    if (hi > mods[idx].end)
        mods[idx].end = hi;
}

static size_t filter_elf_mods(re_dbg_backend_t *b, linux_mod_t *mods, size_t n_mod) {
    size_t kept = 0;
    for (size_t i = 0; i < n_mod; i++) {
        if (!elf_magic_ok(b, mods[i].base))
            continue;
        if (kept != i)
            mods[kept] = mods[i];
        kept++;
    }
    return kept > 0 ? kept : n_mod;
}

static void fill_module_out(re_dbg_module_t *out, size_t cap, const linux_mod_t *mods,
                            size_t n_mod) {
    size_t lim = n_mod < cap ? n_mod : cap;
    for (size_t i = 0; i < lim; i++) {
        re_str_t ps = re_str(mods[i].path);
        memset(&out[i], 0, sizeof(out[i]));
        out[i].base = mods[i].base;
        out[i].size = mods[i].end > mods[i].base ? mods[i].end - mods[i].base : 0;
        copy_path(out[i].path, sizeof(out[i].path), mods[i].path, ps.n);
    }
}

re_err_code_t re_dbg_linux_enum_modules(re_dbg_backend_t *b, re_dbg_module_t *out, size_t cap,
                                        size_t *n_out) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    char maps_path[64];
    char line[512];
    FILE *fp;
    linux_mod_t mods[RE_DBG_LINUX_MAX_MOD];
    size_t n_mod = 0;
    if (n_out)
        *n_out = 0;
    if (!st)
        return RE_E_INTERNAL;
    if (!st->attached)
        return re_dbg_linux_fail(st, RE_E_USAGE, "not attached");
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", (int)st->pid);
    fp = fopen(maps_path, "r");
    if (!fp)
        return re_dbg_linux_fail_errno(st, "fopen /proc/pid/maps");
    while (fgets(line, sizeof(line), fp)) {
        uint64_t lo = 0, hi = 0;
        char path[260];
        if (!parse_map_line(line, &lo, &hi, path, sizeof(path)) || !path_is_file(path))
            continue;
        merge_map(mods, &n_mod, lo, hi, path);
    }
    fclose(fp);
    n_mod = filter_elf_mods(b, mods, n_mod);
    if (n_out)
        *n_out = n_mod;
    if (out && cap)
        fill_module_out(out, cap, mods, n_mod);
    return RE_OK;
}

#else
typedef int re_dbg_linux_mem_placeholder;
#endif /* __linux__ */
