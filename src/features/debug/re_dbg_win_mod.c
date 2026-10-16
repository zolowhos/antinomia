// re_dbg_win_mod.c - Windows module tracking and enum_modules.
// Module: feature (C11).
// Owns: LOAD_DLL/CREATE_PROCESS module table for symbols/bt.
// Depends: re_dbg_win_priv.h. Compiled on all hosts; body is Windows-only.
#include "features/debug/re_dbg.h"

#include <string.h>

#if !defined(_WIN32)

typedef int re_dbg_win_mod_placeholder;

#else /* _WIN32 */

#    include "features/debug/re_dbg_win_priv.h"
#    include "utils/text/re_str.h"

static size_t copy_cstr(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    if (!dst || cap == 0)
        return 0;
    if (!src) {
        dst[0] = 0;
        return 0;
    }
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
    return i;
}

static void strip_nt_prefix(char *path) {
    re_str_t p = re_str(path);
    size_t i;
    if (!re_str_starts_cstr(p, "\\\\?\\"))
        return;
    for (i = 0; i + 4 < p.n; i++)
        path[i] = path[i + 4];
    path[i] = 0;
}

static uint64_t pe_size_of_image(HANDLE process, uint64_t base, HANDLE file) {
    uint8_t buf[0x200];
    SIZE_T got = 0;
    uint32_t e_lfanew = 0;
    uint32_t size = 0;
    (void)file;
    if (process && ReadProcessMemory(process, (LPCVOID)(uintptr_t)base, buf, sizeof(buf), &got) &&
        got >= 0x40 && buf[0] == 'M' && buf[1] == 'Z') {
        e_lfanew = (uint32_t)buf[0x3c] | ((uint32_t)buf[0x3d] << 8) | ((uint32_t)buf[0x3e] << 16) |
                   ((uint32_t)buf[0x3f] << 24);
        if (e_lfanew + 0x50 < got) {
            /* OptionalHeader.SizeOfImage at PE+24+56 for PE32+; PE+24+56 for PE32 too. */
            size = (uint32_t)buf[e_lfanew + 0x50] | ((uint32_t)buf[e_lfanew + 0x51] << 8) |
                   ((uint32_t)buf[e_lfanew + 0x52] << 16) | ((uint32_t)buf[e_lfanew + 0x53] << 24);
            if (size)
                return size;
        }
    }
    return 0x1000;
}

static void path_from_handle(HANDLE file, char *dst, size_t cap) {
    char tmp[MAX_PATH * 2];
    DWORD n;
    dst[0] = 0;
    if (!file || file == INVALID_HANDLE_VALUE)
        return;
    n = GetFinalPathNameByHandleA(file, tmp, (DWORD)sizeof(tmp), FILE_NAME_NORMALIZED);
    if (n == 0 || n >= sizeof(tmp))
        return;
    copy_cstr(dst, cap, tmp);
    strip_nt_prefix(dst);
}

static void path_from_debuggee(HANDLE process, const void *name_ptr, bool unicode, char *dst,
                               size_t cap) {
    uint64_t remote = 0;
    SIZE_T got = 0;
    dst[0] = 0;
    if (!process || !name_ptr)
        return;
    if (!ReadProcessMemory(process, name_ptr, &remote, sizeof(remote), &got) ||
        got < sizeof(void *))
        return;
    if (!remote)
        return;
    if (unicode) {
        WCHAR wbuf[260];
        char abuf[260];
        if (!ReadProcessMemory(process, (LPCVOID)(uintptr_t)remote, wbuf, sizeof(wbuf), &got))
            return;
        wbuf[(sizeof(wbuf) / sizeof(wbuf[0])) - 1] = 0;
        if (WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, abuf, (int)sizeof(abuf), NULL, NULL) <= 0)
            return;
        copy_cstr(dst, cap, abuf);
    } else {
        char abuf[260];
        if (!ReadProcessMemory(process, (LPCVOID)(uintptr_t)remote, abuf, sizeof(abuf), &got))
            return;
        abuf[sizeof(abuf) - 1] = 0;
        copy_cstr(dst, cap, abuf);
    }
}

void re_dbg_win_mod_clear(re_dbg_win_impl_t *w) {
    if (!w)
        return;
    w->n_mod = 0;
    memset(w->mods, 0, sizeof(w->mods));
}

void re_dbg_win_mod_note(re_dbg_win_impl_t *w, uint64_t base, HANDLE file, const void *name_ptr,
                         bool unicode) {
    char path[260];
    size_t i;
    if (!w || base == 0)
        return;
    path[0] = 0;
    path_from_handle(file, path, sizeof(path));
    if (!path[0])
        path_from_debuggee(w->process, name_ptr, unicode, path, sizeof(path));
    for (i = 0; i < w->n_mod; i++) {
        if (w->mods[i].base == base) {
            if (path[0])
                copy_cstr(w->mods[i].path, sizeof(w->mods[i].path), path);
            if (w->mods[i].size == 0)
                w->mods[i].size = pe_size_of_image(w->process, base, file);
            return;
        }
    }
    if (w->n_mod >= RE_DBG_WIN_MAX_MOD)
        return;
    memset(&w->mods[w->n_mod], 0, sizeof(w->mods[w->n_mod]));
    w->mods[w->n_mod].base = base;
    w->mods[w->n_mod].size = pe_size_of_image(w->process, base, file);
    copy_cstr(w->mods[w->n_mod].path, sizeof(w->mods[w->n_mod].path), path);
    w->n_mod++;
}

static void win_mod_del(re_dbg_win_impl_t *w, uint64_t base) {
    size_t i;
    for (i = 0; i < w->n_mod; i++) {
        if (w->mods[i].base != base)
            continue;
        w->mods[i] = w->mods[w->n_mod - 1];
        w->n_mod--;
        return;
    }
}

re_err_code_t re_dbg_win_enum_modules(re_dbg_backend_t *b, re_dbg_module_t *out, size_t cap,
                                      size_t *n_out) {
    re_dbg_win_impl_t *w = re_dbg_win_impl(b);
    size_t n;
    if (n_out)
        *n_out = 0;
    if (!w || !w->active)
        return RE_E_USAGE;
    n = w->n_mod;
    if (n_out)
        *n_out = n;
    if (!out || cap == 0)
        return RE_OK;
    if (n > cap)
        n = cap;
    for (size_t i = 0; i < n; i++) {
        memset(&out[i], 0, sizeof(out[i]));
        out[i].base = w->mods[i].base;
        out[i].size = w->mods[i].size;
        copy_cstr(out[i].path, sizeof(out[i].path), w->mods[i].path);
    }
    return RE_OK;
}



/* Used from re_dbg_win_track when unload arrives. */
void re_dbg_win_mod_unload(re_dbg_win_impl_t *w, uint64_t base) {
    win_mod_del(w, base);
}

#endif /* _WIN32 */
