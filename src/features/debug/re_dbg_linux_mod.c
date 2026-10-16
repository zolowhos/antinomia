// re_dbg_linux_mod.c - Linux module list via /proc/maps and process memory reads.
// Module: feature (C11).
// Owns: enum_modules from maps+ELF headers and read_mem for stack/symbol helpers.
// Depends: re_dbg_linux_priv.h. No malloc; stack scratch only.
#define _GNU_SOURCE
#include "features/debug/re_dbg_linux_priv.h"

#if defined(__linux__)

#    include "utils/text/re_str.h"

#    include <fcntl.h>
#    include <stdio.h>
#    include <string.h>
#    include <sys/uio.h>

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

static re_err_code_t peek_bytes(pid_t pid, uint64_t addr, void *dst, size_t n) {
    uint8_t *out = (uint8_t *)dst;
    size_t done = 0;
    while (done < n) {
        errno = 0;
        long word = ptrace(PTRACE_PEEKDATA, pid, (void *)(uintptr_t)(addr + done), NULL);
        if (word == -1 && errno != 0)
            return RE_E_IO;
        size_t chunk = sizeof(long);
        if (chunk > n - done)
            chunk = n - done;
        memcpy(out + done, &word, chunk);
        done += chunk;
    }
    return RE_OK;
}

re_err_code_t re_dbg_linux_read_mem(re_dbg_backend_t *b, uint64_t addr, void *dst, size_t n) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    struct iovec local, remote;
    ssize_t got;
    if (!st)
        return RE_E_INTERNAL;
    if (!st->attached)
        return re_dbg_linux_fail(st, RE_E_USAGE, "not attached");
    if (!dst || n == 0)
        return RE_E_USAGE;
    local.iov_base = dst;
    local.iov_len = n;
    remote.iov_base = (void *)(uintptr_t)addr;
    remote.iov_len = n;
    got = process_vm_readv(st->pid, &local, 1, &remote, 1, 0);
    if (got == (ssize_t)n)
        return RE_OK;
    return peek_bytes(st->pid, addr, dst, n) == RE_OK
               ? RE_OK
               : re_dbg_linux_fail_errno(st, "process_vm_readv/PTRACE_PEEKDATA");
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
typedef int re_dbg_linux_mod_placeholder;
#endif /* __linux__ */
