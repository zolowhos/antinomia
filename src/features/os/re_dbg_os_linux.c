// re_dbg_os_linux.c - Linux signal disposition and ELF GNU build-id enrich.
// Module: feature/os (C11).
// Owns: /proc status Sig* parse and on-disk ELF note build-id for modules.
// Depends: re_dbg_os.h. Live on Linux; stubs elsewhere.
#include "features/os/re_dbg_os.h"

#include "utils/text/re_str.h"

#include <stdio.h>
#include <string.h>

#if defined(__linux__)
#    include <fcntl.h>
#    include <unistd.h>
#endif

static void copy_cstr(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    if (!dst || cap == 0)
        return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void fill_sig_name(int32_t n, char *out, size_t cap) {
    static const char *const names[] = {
        NULL,        "SIGHUP",  "SIGINT",    "SIGQUIT", "SIGILL",   "SIGTRAP", "SIGABRT", "SIGBUS",
        "SIGFPE",    "SIGKILL", "SIGUSR1",   "SIGSEGV", "SIGUSR2",  "SIGPIPE", "SIGALRM", "SIGTERM",
        "SIGSTKFLT", "SIGCHLD", "SIGCONT",   "SIGSTOP", "SIGTSTP",  "SIGTTIN", "SIGTTOU", "SIGURG",
        "SIGXCPU",   "SIGXFSZ", "SIGVTALRM", "SIGPROF", "SIGWINCH", "SIGIO",   "SIGPWR",  "SIGSYS"};
    if (n > 0 && (size_t)n < sizeof(names) / sizeof(names[0]) && names[n]) {
        copy_cstr(out, cap, names[n]);
        return;
    }
    snprintf(out, cap, "SIG%d", (int)n);
}

#if defined(__linux__)

static bool parse_hex_mask(const char *s, uint64_t *lo, uint64_t *hi) {
    char tmp[40];
    size_t n = 0, i, split = 0;
    uint64_t a = 0, b = 0;
    if (!s || !lo || !hi)
        return false;
    *lo = 0;
    *hi = 0;
    while (*s == ' ' || *s == '\t')
        s++;
    while (s[n] && s[n] != '\n' && s[n] != '\r' && n + 1u < sizeof(tmp)) {
        tmp[n] = s[n];
        n++;
    }
    tmp[n] = 0;
    while (n && (tmp[n - 1] == ' ' || tmp[n - 1] == '\t'))
        tmp[--n] = 0;
    if (n == 0)
        return false;
    for (i = 0; i < n; i++) {
        if (tmp[i] == ' ') {
            split = i;
            break;
        }
    }
    if (split) {
        tmp[split] = 0;
        if (sscanf(tmp, "%llx", (unsigned long long *)&a) != 1)
            return false;
        if (sscanf(tmp + split + 1, "%llx", (unsigned long long *)&b) != 1)
            return false;
        *hi = a;
        *lo = b;
        return true;
    }
    if (sscanf(tmp, "%llx", (unsigned long long *)&a) != 1)
        return false;
    *lo = a;
    *hi = 0;
    return true;
}

static bool bit_set(uint64_t lo, uint64_t hi, int signo) {
    if (signo < 1 || signo > 64)
        return false;
    if (signo > 32)
        return ((hi >> (unsigned)(signo - 33)) & 1ull) != 0;
    return ((lo >> (unsigned)(signo - 1)) & 1ull) != 0;
}

static void read_sig_masks(FILE *fp, uint64_t *blk_lo, uint64_t *blk_hi, uint64_t *ign_lo,
                           uint64_t *ign_hi, uint64_t *cgt_lo, uint64_t *cgt_hi) {
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (memcmp(line, "SigBlk:", 7) == 0)
            (void)parse_hex_mask(line + 7, blk_lo, blk_hi);
        else if (memcmp(line, "SigIgn:", 7) == 0)
            (void)parse_hex_mask(line + 7, ign_lo, ign_hi);
        else if (memcmp(line, "SigCgt:", 7) == 0)
            (void)parse_hex_mask(line + 7, cgt_lo, cgt_hi);
    }
}

static const char *disp_for(uint64_t blk_lo, uint64_t blk_hi, uint64_t ign_lo, uint64_t ign_hi,
                            uint64_t cgt_lo, uint64_t cgt_hi, int signo) {
    if (bit_set(ign_lo, ign_hi, signo))
        return "ignored";
    if (bit_set(cgt_lo, cgt_hi, signo))
        return "caught";
    if (bit_set(blk_lo, blk_hi, signo))
        return "blocked";
    return "default";
}

re_err_code_t re_dbg_session_enum_signals(re_dbg_session_t *s, re_dbg_sig_disp_t *out, size_t cap,
                                          size_t *n_out) {
    char path[64];
    FILE *fp;
    uint64_t blk_lo = 0, blk_hi = 0, ign_lo = 0, ign_hi = 0, cgt_lo = 0, cgt_hi = 0;
    size_t n = 0;
    int signo;
    if (n_out)
        *n_out = 0;
    if (!s || !s->attached || s->pid == 0)
        return RE_E_USAGE;
    snprintf(path, sizeof(path), "/proc/%u/status", (unsigned)s->pid);
    fp = fopen(path, "r");
    if (!fp)
        return RE_E_IO;
    read_sig_masks(fp, &blk_lo, &blk_hi, &ign_lo, &ign_hi, &cgt_lo, &cgt_hi);
    fclose(fp);
    for (signo = 1; signo <= 64 && n < RE_DBG_SIG_MAX; signo++) {
        if (out && n < cap) {
            memset(&out[n], 0, sizeof(out[n]));
            out[n].signo = signo;
            fill_sig_name(signo, out[n].name, sizeof(out[n].name));
            copy_cstr(out[n].disposition, sizeof(out[n].disposition),
                      disp_for(blk_lo, blk_hi, ign_lo, ign_hi, cgt_lo, cgt_hi, signo));
        }
        n++;
    }
    if (n_out)
        *n_out = n;
    return RE_OK;
}

static void hex_encode(const uint8_t *in, size_t n, char *out, size_t cap) {
    static const char *hex = "0123456789abcdef";
    size_t i, o = 0;
    if (!out || cap == 0)
        return;
    for (i = 0; i < n && o + 2u < cap; i++) {
        out[o++] = hex[(in[i] >> 4) & 0xf];
        out[o++] = hex[in[i] & 0xf];
    }
    out[o] = 0;
}

/* Scan one PT_NOTE segment for NT_GNU_BUILD_ID. Returns true on hit. */
static bool note_scan_build_id(FILE *fp, uint64_t filesz, char *out, size_t cap) {
    uint64_t pos = 0;
    while (pos + 12u <= filesz) {
        uint32_t namesz = 0, descsz = 0, ntype = 0;
        uint8_t name[16];
        uint8_t desc[32];
        uint32_t name_pad, desc_pad;
        size_t got;
        if (fread(&namesz, 4, 1, fp) != 1 || fread(&descsz, 4, 1, fp) != 1 ||
            fread(&ntype, 4, 1, fp) != 1)
            return false;
        pos += 12u;
        name_pad = (namesz + 3u) & ~3u;
        desc_pad = (descsz + 3u) & ~3u;
        if (pos + name_pad + desc_pad > filesz)
            return false;
        memset(name, 0, sizeof(name));
        got = namesz < sizeof(name) ? namesz : sizeof(name) - 1u;
        if (got && fread(name, 1, got, fp) != got)
            return false;
        if (namesz > got && fseek(fp, (long)(namesz - got), SEEK_CUR) != 0)
            return false;
        if (name_pad > namesz && fseek(fp, (long)(name_pad - namesz), SEEK_CUR) != 0)
            return false;
        pos += name_pad;
        if (ntype == 3u && namesz >= 3 && name[0] == 'G' && name[1] == 'N' && name[2] == 'U' &&
            descsz > 0 && descsz <= sizeof(desc)) {
            if (fread(desc, 1, descsz, fp) != descsz)
                return false;
            hex_encode(desc, descsz, out, cap);
            return out[0] != 0;
        }
        if (desc_pad && fseek(fp, (long)desc_pad, SEEK_CUR) != 0)
            return false;
        pos += desc_pad;
    }
    return false;
}

static bool elf_build_id_file(const char *path, char *out, size_t cap) {
    FILE *fp;
    uint8_t ehdr[64];
    uint16_t phnum = 0, phentsize = 0, i;
    uint64_t phoff = 0;
    if (!path || !path[0] || path[0] == '[' || !out || cap < 8)
        return false;
    out[0] = 0;
    fp = fopen(path, "rb");
    if (!fp)
        return false;
    if (fread(ehdr, 1, 64, fp) != 64 || ehdr[0] != 0x7f || ehdr[1] != 'E' || ehdr[2] != 'L' ||
        ehdr[3] != 'F' || ehdr[4] != 2) {
        fclose(fp);
        return false;
    }
    memcpy(&phoff, ehdr + 32, 8);
    memcpy(&phentsize, ehdr + 54, 2);
    memcpy(&phnum, ehdr + 56, 2);
    if (phentsize < 56 || phnum == 0 || phnum > 512) {
        fclose(fp);
        return false;
    }
    for (i = 0; i < phnum; i++) {
        uint8_t ph[56];
        uint32_t type = 0;
        uint64_t off = 0, filesz = 0;
        if (fseek(fp, (long)(phoff + (uint64_t)i * phentsize), SEEK_SET) != 0)
            break;
        if (fread(ph, 1, 56, fp) != 56)
            break;
        memcpy(&type, ph, 4);
        if (type != 4u)
            continue;
        memcpy(&off, ph + 8, 8);
        memcpy(&filesz, ph + 32, 8);
        if (filesz == 0 || filesz > 1u << 20)
            continue;
        if (fseek(fp, (long)off, SEEK_SET) != 0)
            continue;
        if (note_scan_build_id(fp, filesz, out, cap)) {
            fclose(fp);
            return true;
        }
    }
    fclose(fp);
    return false;
}

re_err_code_t re_dbg_os_enrich_modules(re_dbg_session_t *s, re_dbg_module_t *mods, size_t n) {
    size_t i;
    (void)s;
    if (!mods)
        return RE_E_USAGE;
    for (i = 0; i < n; i++) {
        if (mods[i].build_id[0])
            continue;
        (void)elf_build_id_file(mods[i].path, mods[i].build_id, sizeof(mods[i].build_id));
    }
    return RE_OK;
}

#else /* !__linux__ */

re_err_code_t re_dbg_session_enum_signals(re_dbg_session_t *s, re_dbg_sig_disp_t *out, size_t cap,
                                          size_t *n_out) {
    (void)s;
    (void)out;
    (void)cap;
    if (n_out)
        *n_out = 0;
    return RE_E_UNSUPPORTED;
}

re_err_code_t re_dbg_os_enrich_modules(re_dbg_session_t *s, re_dbg_module_t *mods, size_t n) {
    (void)s;
    (void)mods;
    (void)n;
    return RE_OK;
}

#endif

re_err_code_t re_dbg_session_db_save(re_dbg_session_t *s) {
    re_arena_t scratch;
    re_err_code_t c;
    if (!s)
        return RE_E_INTERNAL;
    if (!s->db.db_path[0] && s->path[0]) {
        re_arena_init(&scratch, 8u * 1024u);
        (void)re_dbg_db_load(&s->db, &scratch, s->path);
        re_arena_free(&scratch);
    }
    if (!s->db.db_path[0])
        return RE_E_USAGE;
    s->db.dirty = true;
    re_arena_init(&scratch, 64u * 1024u);
    c = re_dbg_db_save(&s->db, &scratch);
    re_arena_free(&scratch);
    if (c == RE_OK)
        s->db.dirty = false;
    return c;
}
