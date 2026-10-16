// re_dbg_win_mem.c - registers and memory ops for the Windows debug backend.
// Module: feature (C11).
// Owns: Get/SetThreadContext GPR mapping and Read/WriteProcessMemory.
// Depends: re_dbg_win_priv.h. FlushInstructionCache runs after every write.
#include "features/debug/re_dbg.h"

#include <string.h>

#if !defined(_WIN32)

typedef int re_dbg_win_mem_placeholder;

#else /* _WIN32 */

#    include "features/debug/re_dbg_win_priv.h"

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

#endif /* _WIN32 */
