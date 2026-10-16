// re_sym_export.c - PE export / ELF dynsym symbol provider behind the vtable.
// Module: feature/sym (C11).
// Owns: the always-on file-backed resolve provider used before PDB/DWARF.
// Depends: re_sym_prov.h, re_dbg_sym_resolve_file. No OS debug APIs.
#include "features/sym/re_sym_prov.h"

#include "utils/sys/re_path.h"

#include <string.h>

typedef struct {
    re_arena_t *arena;
    char path[RE_SYM_PATH_MAX];
    uint64_t base;
    bool open;
} export_impl_t;

static void copy_name(char *dst, size_t cap, const char *src) {
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

static re_err_code_t exp_open(re_sym_provider_t *p, re_arena_t *a, const char *path,
                              uint64_t base) {
    export_impl_t *im;
    if (!p || !p->impl || !a || !path || !path[0])
        return RE_E_USAGE;
    im = (export_impl_t *)p->impl;
    im->arena = a;
    copy_name(im->path, sizeof(im->path), path);
    im->base = base;
    im->open = true;
    return RE_OK;
}

static re_err_code_t exp_close(re_sym_provider_t *p) {
    export_impl_t *im;
    if (!p || !p->impl)
        return RE_E_USAGE;
    im = (export_impl_t *)p->impl;
    im->open = false;
    im->path[0] = 0;
    im->base = 0;
    return RE_OK;
}

static re_err_code_t exp_resolve(re_sym_provider_t *p, const char *name, uint64_t *addr_out,
                                 re_dbg_symbol_t *info_out) {
    export_impl_t *im;
    re_err_code_t c;
    uint64_t addr = 0;
    if (!p || !p->impl || !name || !name[0])
        return RE_E_USAGE;
    im = (export_impl_t *)p->impl;
    if (!im->open || !im->arena)
        return RE_E_USAGE;
    if (addr_out)
        *addr_out = 0;
    if (info_out)
        memset(info_out, 0, sizeof(*info_out));
    c = re_dbg_sym_resolve_file(im->arena, im->path, im->base, name, &addr);
    if (c != RE_OK)
        return c;
    if (addr_out)
        *addr_out = addr;
    if (info_out) {
        info_out->addr = addr;
        copy_name(info_out->name, sizeof(info_out->name), name);
        copy_name(info_out->module, sizeof(info_out->module), re_path_basename_ptr(im->path));
    }
    return RE_OK;
}

static re_err_code_t exp_addr_sym(re_sym_provider_t *p, uint64_t addr, re_dbg_symbol_t *info_out) {
    (void)p;
    (void)addr;
    if (info_out)
        memset(info_out, 0, sizeof(*info_out));
    return RE_E_UNSUPPORTED;
}

static re_err_code_t exp_addr_line(re_sym_provider_t *p, uint64_t addr, re_sym_line_t *out) {
    (void)p;
    (void)addr;
    if (out)
        memset(out, 0, sizeof(*out));
    return RE_E_UNSUPPORTED;
}

static void exp_destroy(re_sym_provider_t *p) {
    (void)p;
}

static const re_sym_provider_vtbl_t k_export_vt = {
    "export", exp_open, exp_close, exp_resolve, exp_addr_sym, exp_addr_line, exp_destroy};

re_sym_provider_t *re_sym_export_create(re_arena_t *a) {
    re_sym_provider_t *p;
    export_impl_t *im;
    if (!a)
        return NULL;
    p = (re_sym_provider_t *)re_arena_alloc(a, sizeof(*p));
    im = (export_impl_t *)re_arena_alloc(a, sizeof(*im));
    if (!p || !im)
        return NULL;
    memset(im, 0, sizeof(*im));
    p->vt = &k_export_vt;
    p->impl = im;
    return p;
}
