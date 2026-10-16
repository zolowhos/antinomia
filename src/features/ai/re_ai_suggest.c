// re_ai_suggest.c - suggest software BP sites on interesting imports.
// Module: feature/ai (C11).
// Owns: fixed import catalog + session resolve for suggest_bp.
// Depends: re_ai.h, re_dbg_session_resolve. Deterministic; no cloud.
#include "features/ai/re_ai.h"

#include <string.h>

typedef struct {
    const char *name;
    const char *reason;
} re_ai_import_t;

/* Dual-OS interesting imports: resolve by bare symbol across loaded modules. */
static const re_ai_import_t kImports[] = {
    {"malloc", "heap allocation"},
    {"calloc", "heap allocation"},
    {"realloc", "heap reallocation"},
    {"free", "heap free"},
    {"mmap", "map anonymous/file memory"},
    {"mprotect", "change page protection"},
    {"open", "open file descriptor"},
    {"openat", "open file descriptor"},
    {"read", "read from fd"},
    {"write", "write to fd"},
    {"execve", "process image replace"},
    {"ptrace", "tracer / anti-debug surface"},
    {"dlopen", "load shared library"},
    {"CreateFileW", "open file (Win)"},
    {"CreateFileA", "open file (Win)"},
    {"VirtualAlloc", "allocate virtual memory"},
    {"VirtualProtect", "change page protection"},
    {"LoadLibraryA", "load module"},
    {"LoadLibraryW", "load module"},
    {"GetProcAddress", "resolve export by name"},
    {"IsDebuggerPresent", "anti-debug check"},
    {"NtCreateFile", "NT open file"},
    {"NtAllocateVirtualMemory", "NT virtual alloc"},
    {"WriteProcessMemory", "cross-process write"},
};

static void copy_cap(char *dst, size_t cap, const char *src) {
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

re_err_code_t re_ai_suggest_bp(re_dbg_session_t *s, re_ai_bp_site_t *out, size_t cap,
                               size_t *n_out) {
    size_t n = 0, i;
    if (!out || !n_out || cap == 0)
        return RE_E_USAGE;
    *n_out = 0;
    if (!s || !s->attached)
        return RE_E_USAGE;
    for (i = 0; i < sizeof(kImports) / sizeof(kImports[0]) && n < cap; i++) {
        re_dbg_symbol_t info;
        uint64_t addr = 0;
        re_err_code_t c;
        memset(&info, 0, sizeof(info));
        c = re_dbg_session_resolve(s, kImports[i].name, &addr, &info);
        if (c != RE_OK)
            continue;
        memset(&out[n], 0, sizeof(out[n]));
        if (info.module[0] && info.name[0]) {
            size_t k = 0;
            const char *mod = info.module;
            const char *sym = info.name;
            while (mod[k] && k + 1u < sizeof(out[n].query)) {
                out[n].query[k] = mod[k];
                k++;
            }
            if (k + 1u < sizeof(out[n].query))
                out[n].query[k++] = ':';
            {
                size_t j = 0;
                while (sym[j] && k + 1u < sizeof(out[n].query)) {
                    out[n].query[k++] = sym[j++];
                }
                out[n].query[k] = 0;
            }
        } else {
            copy_cap(out[n].query, sizeof(out[n].query), kImports[i].name);
        }
        copy_cap(out[n].reason, sizeof(out[n].reason), kImports[i].reason);
        out[n].addr = addr;
        out[n].resolved = true;
        n++;
    }
    *n_out = n;
    return RE_OK;
}
