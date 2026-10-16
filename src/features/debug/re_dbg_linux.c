// re_dbg_linux.c - Linux ptrace backend stub (Phase 0: links, not live).
// Module: feature (C11).
// Owns: the Linux stub vtable that returns RE_E_UNSUPPORTED for every op.
// Depends: re_dbg.h. Real ptrace arrives in Phase 1 pack A.
#include "features/debug/re_dbg.h"

#include <string.h>

static re_err_code_t stub_launch(re_dbg_backend_t *b, const char *path, const char *args) {
    (void)b;
    (void)path;
    (void)args;
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_attach(re_dbg_backend_t *b, uint32_t pid) {
    (void)b;
    (void)pid;
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_detach(re_dbg_backend_t *b) {
    (void)b;
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_kill(re_dbg_backend_t *b) {
    (void)b;
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_wait(re_dbg_backend_t *b, re_dbg_event_t *ev, uint32_t timeout_ms) {
    (void)b;
    (void)timeout_ms;
    if (ev)
        memset(ev, 0, sizeof(*ev));
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_cont(re_dbg_backend_t *b) {
    (void)b;
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_step(re_dbg_backend_t *b) {
    (void)b;
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_get_regs(re_dbg_backend_t *b, uint32_t tid, re_dbg_regs_t *out) {
    (void)b;
    (void)tid;
    if (out)
        memset(out, 0, sizeof(*out));
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_set_regs(re_dbg_backend_t *b, uint32_t tid, const re_dbg_regs_t *in) {
    (void)b;
    (void)tid;
    (void)in;
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_read(re_dbg_backend_t *b, uint64_t addr, void *dst, size_t n) {
    (void)b;
    (void)addr;
    if (dst && n)
        memset(dst, 0, n);
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_write(re_dbg_backend_t *b, uint64_t addr, const void *src, size_t n) {
    (void)b;
    (void)addr;
    (void)src;
    (void)n;
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_threads(re_dbg_backend_t *b, re_dbg_thread_t *out, size_t cap,
                                  size_t *n_out) {
    (void)b;
    (void)out;
    (void)cap;
    if (n_out)
        *n_out = 0;
    return RE_E_UNSUPPORTED;
}
static re_err_code_t stub_modules(re_dbg_backend_t *b, re_dbg_module_t *out, size_t cap,
                                  size_t *n_out) {
    (void)b;
    (void)out;
    (void)cap;
    if (n_out)
        *n_out = 0;
    return RE_E_UNSUPPORTED;
}
static void stub_destroy(re_dbg_backend_t *b) {
    (void)b;
}
static const re_dbg_backend_vtbl_t kVtbl = {
    stub_launch,    stub_attach,   stub_detach, stub_kill,    stub_wait,    stub_cont,
    stub_step,      stub_get_regs, stub_set_regs, stub_read,  stub_write,   stub_threads,
    stub_modules,   stub_destroy};

re_dbg_backend_t *re_dbg_backend_linux_create(re_arena_t *a) {
    re_dbg_backend_t *b = (re_dbg_backend_t *)re_arena_alloc(a, sizeof(*b));
    if (!b)
        return NULL;
    b->vt = &kVtbl;
    b->impl = NULL;
    return b;
}
