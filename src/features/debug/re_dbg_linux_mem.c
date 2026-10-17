// re_dbg_linux_mem.c - process memory read/write for the Linux ptrace backend.
// Module: feature (C11).
// Owns: process_vm/ptrace mem R/W and enum_modules from /proc maps.
// Depends: re_dbg_linux_priv.h, re_str. Disasm cache invalidation is session-side.
#define _GNU_SOURCE
#include "features/debug/re_dbg_linux_priv.h"

#if defined(__linux__)

#    include "utils/text/re_str.h"

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

static bool parse_map_line(const char *line, uint64_t *lo, uint64_t *hi, char *perms_out,
                           char *path, size_t path_cap) {
    unsigned long long a = 0, b = 0;
    char perms[8];
    unsigned long long off = 0;
    char dev[16];
    unsigned long long inode = 0;
    const char *p;
    int n;
    path[0] = 0;
    if (perms_out)
        perms_out[0] = 0;
    n = sscanf(line, "%llx-%llx %7s %llx %15s %llu", &a, &b, perms, &off, dev, &inode);
    if (n < 6)
        return false;
    *lo = (uint64_t)a;
    *hi = (uint64_t)b;
    if (perms_out) {
        size_t i = 0;
        while (perms[i] && i + 1 < 8) {
            perms_out[i] = perms[i];
            i++;
        }
        perms_out[i] = 0;
    }
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

static uint32_t prot_from_perms(const char *perms) {
    uint32_t p = 0;
    if (!perms)
        return 0;
    if (perms[0] == 'r')
        p |= RE_DBG_PROT_R;
    if (perms[1] == 'w')
        p |= RE_DBG_PROT_W;
    if (perms[2] == 'x')
        p |= RE_DBG_PROT_X;
    return p;
}

static re_dbg_map_type_t map_type_from(const char *path, const char *perms) {
    if (path && path[0] == '[') {
        if (re_str_starts_cstr(re_str(path), "[stack"))
            return RE_DBG_MAP_STACK;
        if (re_str_starts_cstr(re_str(path), "[heap"))
            return RE_DBG_MAP_HEAP;
        return RE_DBG_MAP_OTHER;
    }
    if (path && path[0] == '/') {
        if (perms && perms[3] == 'p')
            return RE_DBG_MAP_IMAGE;
        return RE_DBG_MAP_MAPPED;
    }
    if (perms && perms[3] == 'p')
        return RE_DBG_MAP_PRIVATE;
    if (perms && perms[3] == 's')
        return RE_DBG_MAP_MAPPED;
    return RE_DBG_MAP_OTHER;
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
        if (!parse_map_line(line, &lo, &hi, NULL, path, sizeof(path)) || !path_is_file(path))
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

#    define RE_DBG_LINUX_MAX_MAP 256

re_err_code_t re_dbg_linux_enum_maps(re_dbg_backend_t *b, re_dbg_map_t *out, size_t cap,
                                     size_t *n_out) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    char maps_path[64];
    char line[512];
    FILE *fp;
    size_t n = 0;
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
    while (fgets(line, sizeof(line), fp) && n < RE_DBG_LINUX_MAX_MAP) {
        uint64_t lo = 0, hi = 0;
        char path[260];
        char perms[8];
        if (!parse_map_line(line, &lo, &hi, perms, path, sizeof(path)))
            continue;
        if (out && n < cap) {
            memset(&out[n], 0, sizeof(out[n]));
            out[n].base = lo;
            out[n].size = hi > lo ? hi - lo : 0;
            out[n].protect = prot_from_perms(perms);
            out[n].type = map_type_from(path, perms);
            copy_path(out[n].path, sizeof(out[n].path), path, re_str(path).n);
        }
        n++;
    }
    fclose(fp);
    if (n_out)
        *n_out = n;
    return RE_OK;
}

#    define RE_ELF_DT_DEBUG 21
#    define RE_ELF_PT_DYNAMIC 2
#    define RE_ELF_PT_PHDR 6

static re_err_code_t read_u32(re_dbg_backend_t *b, uint64_t addr, uint32_t *out) {
    return re_dbg_linux_read_mem(b, addr, out, sizeof(*out));
}

static re_err_code_t read_u64(re_dbg_backend_t *b, uint64_t addr, uint64_t *out) {
    return re_dbg_linux_read_mem(b, addr, out, sizeof(*out));
}

static bool auxv_phdr(re_dbg_linux_t *st, uint64_t *phdr, uint64_t *phnum) {
    char path[64];
    FILE *fp;
    uint64_t pair[2];
    *phdr = 0;
    *phnum = 0;
    snprintf(path, sizeof(path), "/proc/%d/auxv", (int)st->pid);
    fp = fopen(path, "rb");
    if (!fp)
        return false;
    while (fread(pair, sizeof(pair), 1, fp) == 1) {
        if (pair[0] == 0)
            break;
        if (pair[0] == 3u) /* AT_PHDR */
            *phdr = pair[1];
        if (pair[0] == 5u) /* AT_PHNUM */
            *phnum = pair[1];
    }
    fclose(fp);
    return *phdr != 0 && *phnum != 0;
}

static re_err_code_t find_dt_debug(re_dbg_backend_t *b, uint64_t phdr, uint64_t phnum,
                                   uint64_t *debug_out) {
    uint64_t i;
    uint64_t dyn_vaddr = 0;
    uint64_t dyn_filesz = 0;
    uint64_t load_bias = 0;
    *debug_out = 0;
    for (i = 0; i < phnum; i++) {
        uint64_t off = phdr + i * 56u; /* Elf64_Phdr */
        uint32_t type = 0;
        uint64_t vaddr = 0, filesz = 0;
        if (read_u32(b, off, &type) != RE_OK)
            return RE_E_IO;
        if (type == RE_ELF_PT_PHDR) {
            if (read_u64(b, off + 16u, &vaddr) != RE_OK)
                return RE_E_IO;
            load_bias = phdr - vaddr;
        }
        if (type == RE_ELF_PT_DYNAMIC) {
            if (read_u64(b, off + 16u, &vaddr) != RE_OK)
                return RE_E_IO;
            if (read_u64(b, off + 32u, &filesz) != RE_OK)
                return RE_E_IO;
            dyn_vaddr = vaddr;
            dyn_filesz = filesz;
        }
    }
    if (dyn_vaddr == 0)
        return RE_E_NOTFOUND;
    {
        uint64_t dyn = dyn_vaddr + load_bias;
        uint64_t end = dyn + dyn_filesz;
        while (dyn + 16u <= end) {
            uint64_t tag = 0, val = 0;
            if (read_u64(b, dyn, &tag) != RE_OK || read_u64(b, dyn + 8u, &val) != RE_OK)
                return RE_E_IO;
            if (tag == 0)
                break;
            if (tag == (uint64_t)RE_ELF_DT_DEBUG) {
                *debug_out = val;
                return RE_OK;
            }
            dyn += 16u;
        }
    }
    return RE_E_NOTFOUND;
}

re_err_code_t re_dbg_linux_rendezvous_addr(re_dbg_backend_t *b, uint64_t *addr_out) {
    re_dbg_linux_t *st = re_dbg_linux(b);
    uint64_t phdr = 0, phnum = 0, r_debug = 0, r_brk = 0;
    re_err_code_t c;
    if (addr_out)
        *addr_out = 0;
    if (!st)
        return RE_E_INTERNAL;
    if (!st->attached)
        return re_dbg_linux_fail(st, RE_E_USAGE, "not attached");
    if (!addr_out)
        return RE_E_USAGE;
    if (!auxv_phdr(st, &phdr, &phnum))
        return re_dbg_linux_fail(st, RE_E_IO, "auxv AT_PHDR missing");
    c = find_dt_debug(b, phdr, phnum, &r_debug);
    if (c != RE_OK)
        return re_dbg_linux_fail(st, c, "DT_DEBUG not found");
    if (r_debug == 0)
        return re_dbg_linux_fail(st, RE_E_IO, "r_debug is null");
    /* struct r_debug: r_brk at offset 16 on LP64. */
    c = read_u64(b, r_debug + 16u, &r_brk);
    if (c != RE_OK)
        return c;
    if (r_brk == 0)
        return re_dbg_linux_fail(st, RE_E_IO, "r_debug.r_brk is null");
    *addr_out = r_brk;
    return RE_OK;
}

#else
typedef int re_dbg_linux_mem_placeholder;
#endif /* __linux__ */
