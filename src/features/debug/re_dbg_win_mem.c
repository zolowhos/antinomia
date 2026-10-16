// re_dbg_win_mem.c - registers and memory ops for the Windows debug backend.
// Module: feature (C11).
// Owns: Win regs/mem R/W and LOAD_DLL module tracking for enum_modules.
// Depends: re_dbg_win_priv.h, re_str. FlushInstructionCache after writes.
#include "features/debug/re_dbg.h"

#include <string.h>

#if !defined(_WIN32)

typedef int re_dbg_win_mem_placeholder;

#else /* _WIN32 */

#    include "features/debug/re_dbg_win_priv.h"
#    include "utils/text/re_str.h"

static HANDLE win_thr_handle(re_dbg_win_impl_t *w, DWORD tid) {
    for (size_t i = 0; i < w->n_thr; i++) {
        if (w->thr[i].tid == tid)
            return w->thr[i].handle;
    }
    return NULL;
}

static DWORD win_pick_tid(re_dbg_win_impl_t *w, uint32_t tid) {
    if (tid)
        return (DWORD)tid;
    if (w->last_tid)
        return w->last_tid;
    if (w->n_thr)
        return w->thr[0].tid;
    return 0;
}

static void win_fill_regs(re_dbg_regs_t *out, const CONTEXT *c) {
    out->rax = c->Rax;
    out->rbx = c->Rbx;
    out->rcx = c->Rcx;
    out->rdx = c->Rdx;
    out->rsi = c->Rsi;
    out->rdi = c->Rdi;
    out->rbp = c->Rbp;
    out->rsp = c->Rsp;
    out->r8 = c->R8;
    out->r9 = c->R9;
    out->r10 = c->R10;
    out->r11 = c->R11;
    out->r12 = c->R12;
    out->r13 = c->R13;
    out->r14 = c->R14;
    out->r15 = c->R15;
    out->rip = c->Rip;
    out->rflags = c->EFlags;
}

static void win_apply_regs(CONTEXT *c, const re_dbg_regs_t *in) {
    c->Rax = in->rax;
    c->Rbx = in->rbx;
    c->Rcx = in->rcx;
    c->Rdx = in->rdx;
    c->Rsi = in->rsi;
    c->Rdi = in->rdi;
    c->Rbp = in->rbp;
    c->Rsp = in->rsp;
    c->R8 = in->r8;
    c->R9 = in->r9;
    c->R10 = in->r10;
    c->R11 = in->r11;
    c->R12 = in->r12;
    c->R13 = in->r13;
    c->R14 = in->r14;
    c->R15 = in->r15;
    c->Rip = in->rip;
    c->EFlags = (DWORD)in->rflags;
}

re_err_code_t re_dbg_win_get_regs(re_dbg_backend_t *b, uint32_t tid, re_dbg_regs_t *out) {
    re_dbg_win_impl_t *w = re_dbg_win_impl(b);
    CONTEXT ctx;
    HANDLE th;
    DWORD t;
    if (out)
        memset(out, 0, sizeof(*out));
    if (!w || !w->active)
        return RE_E_USAGE;
    if (!out)
        return RE_E_USAGE;
    t = win_pick_tid(w, tid);
    th = win_thr_handle(w, t);
    if (!th || !t)
        return RE_E_NOTFOUND;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (!GetThreadContext(th, &ctx))
        return RE_E_IO;
    win_fill_regs(out, &ctx);
    return RE_OK;
}

re_err_code_t re_dbg_win_set_regs(re_dbg_backend_t *b, uint32_t tid, const re_dbg_regs_t *in) {
    re_dbg_win_impl_t *w = re_dbg_win_impl(b);
    CONTEXT ctx;
    HANDLE th;
    DWORD t;
    if (!w || !w->active)
        return RE_E_USAGE;
    if (!in)
        return RE_E_USAGE;
    t = win_pick_tid(w, tid);
    th = win_thr_handle(w, t);
    if (!th || !t)
        return RE_E_NOTFOUND;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (!GetThreadContext(th, &ctx))
        return RE_E_IO;
    win_apply_regs(&ctx, in);
    ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (!SetThreadContext(th, &ctx))
        return RE_E_IO;
    return RE_OK;
}

re_err_code_t re_dbg_win_read_mem(re_dbg_backend_t *b, uint64_t addr, void *dst, size_t n) {
    re_dbg_win_impl_t *w = re_dbg_win_impl(b);
    SIZE_T got = 0;
    if (dst && n)
        memset(dst, 0, n);
    if (!w || !w->active)
        return RE_E_USAGE;
    if (!dst && n)
        return RE_E_USAGE;
    if (n == 0)
        return RE_OK;
    if (!w->process)
        return RE_E_IO;
    if (!ReadProcessMemory(w->process, (LPCVOID)(uintptr_t)addr, dst, n, &got))
        return RE_E_IO;
    if (got != n)
        return RE_E_IO;
    return RE_OK;
}

re_err_code_t re_dbg_win_write_mem(re_dbg_backend_t *b, uint64_t addr, const void *src, size_t n) {
    re_dbg_win_impl_t *w = re_dbg_win_impl(b);
    SIZE_T got = 0;
    if (!w || !w->active)
        return RE_E_USAGE;
    if ((!src && n) || n == 0)
        return n == 0 ? RE_OK : RE_E_USAGE;
    if (!w->process)
        return RE_E_IO;
    if (!WriteProcessMemory(w->process, (LPVOID)(uintptr_t)addr, src, n, &got))
        return RE_E_IO;
    if (got != n)
        return RE_E_IO;
    FlushInstructionCache(w->process, (LPCVOID)(uintptr_t)addr, n);
    return RE_OK;
}

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

static DWORD win_len_bits(uint8_t size) {
    if (size == 2u)
        return 1u;
    if (size == 8u)
        return 2u;
    if (size == 4u)
        return 3u;
    return 0u;
}

static re_err_code_t win_get_dr_ctx(re_dbg_win_impl_t *w, uint32_t tid, CONTEXT *ctx) {
    HANDLE th;
    memset(ctx, 0, sizeof(*ctx));
    ctx->ContextFlags = CONTEXT_DEBUG_REGISTERS;
    th = re_dbg_win_thread_handle(w, tid ? (DWORD)tid : w->last_tid);
    if (!th)
        return RE_E_NOTFOUND;
    if (!GetThreadContext(th, ctx))
        return RE_E_IO;
    return RE_OK;
}

static re_err_code_t win_set_dr_ctx(re_dbg_win_impl_t *w, uint32_t tid, CONTEXT *ctx) {
    HANDLE th = re_dbg_win_thread_handle(w, tid ? (DWORD)tid : w->last_tid);
    if (!th)
        return RE_E_NOTFOUND;
    ctx->ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!SetThreadContext(th, ctx))
        return RE_E_IO;
    return RE_OK;
}

static DWORD64 *win_dr_ptr(CONTEXT *ctx, int slot) {
    if (slot == 0)
        return &ctx->Dr0;
    if (slot == 1)
        return &ctx->Dr1;
    if (slot == 2)
        return &ctx->Dr2;
    return &ctx->Dr3;
}

re_err_code_t re_dbg_win_get_caps(re_dbg_backend_t *b, re_dbg_caps_t *out) {
    (void)b;
    if (!out)
        return RE_E_USAGE;
    memset(out, 0, sizeof(*out));
    out->hw_bp = true;
    out->mem_bp = true; /* DR watch; page-guard not yet; docs/bp-fidelity.md */
    out->load_bp = true;
    out->hw_slots = 4u;
    return RE_OK;
}

re_err_code_t re_dbg_win_hw_bp_set(re_dbg_backend_t *b, int slot, uint64_t addr, uint8_t size,
                                   uint8_t access) {
    re_dbg_win_impl_t *w = re_dbg_win_impl(b);
    CONTEXT ctx;
    DWORD shift;
    re_err_code_t c;
    if (!w || !w->active)
        return RE_E_USAGE;
    if (slot < 0 || slot > 3)
        return RE_E_RANGE;
    if (access != 0u && access != 1u && access != 3u)
        return RE_E_USAGE;
    c = win_get_dr_ctx(w, 0, &ctx);
    if (c != RE_OK)
        return c;
    *win_dr_ptr(&ctx, slot) = (DWORD64)addr;
    ctx.Dr7 |= (1ull << (unsigned)(slot * 2));
    shift = 16u + (DWORD)slot * 4u;
    ctx.Dr7 &= ~(0xfull << shift);
    ctx.Dr7 |= ((DWORD64)access & 3u) << shift;
    ctx.Dr7 |= ((DWORD64)win_len_bits(size) & 3u) << (shift + 2u);
    return win_set_dr_ctx(w, 0, &ctx);
}

re_err_code_t re_dbg_win_hw_bp_clear(re_dbg_backend_t *b, int slot) {
    re_dbg_win_impl_t *w = re_dbg_win_impl(b);
    CONTEXT ctx;
    DWORD shift;
    re_err_code_t c;
    if (!w || !w->active)
        return RE_E_USAGE;
    if (slot < 0 || slot > 3)
        return RE_E_RANGE;
    c = win_get_dr_ctx(w, 0, &ctx);
    if (c != RE_OK)
        return c;
    ctx.Dr7 &= ~(3ull << (unsigned)(slot * 2));
    shift = 16u + (DWORD)slot * 4u;
    ctx.Dr7 &= ~(0xfull << shift);
    *win_dr_ptr(&ctx, slot) = 0;
    return win_set_dr_ctx(w, 0, &ctx);
}

re_err_code_t re_dbg_win_hw_bp_hit(re_dbg_backend_t *b, uint32_t tid, int *slot_out,
                                   uint64_t *addr_out) {
    re_dbg_win_impl_t *w = re_dbg_win_impl(b);
    CONTEXT ctx;
    int i;
    re_err_code_t c;
    if (slot_out)
        *slot_out = -1;
    if (addr_out)
        *addr_out = 0;
    if (!w || !w->active)
        return RE_E_USAGE;
    c = win_get_dr_ctx(w, tid, &ctx);
    if (c != RE_OK)
        return c;
    for (i = 0; i < 4; i++) {
        if ((ctx.Dr6 & (1ull << (unsigned)i)) == 0)
            continue;
        if (slot_out)
            *slot_out = i;
        if (addr_out)
            *addr_out = (uint64_t)*win_dr_ptr(&ctx, i);
        ctx.Dr6 = 0;
        (void)win_set_dr_ctx(w, tid, &ctx);
        return RE_OK;
    }
    return RE_E_NOTFOUND;
}

re_err_code_t re_dbg_win_rendezvous_addr(re_dbg_backend_t *b, uint64_t *addr_out) {
    (void)b;
    if (addr_out)
        *addr_out = 0; /* LOAD_DLL_DEBUG_EVENT covers module-load BP */
    return RE_OK;
}

#endif /* _WIN32 */
