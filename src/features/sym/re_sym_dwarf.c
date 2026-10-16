// re_sym_dwarf.c - DWARF line-table symbol provider for source view v1.
// Module: feature/sym (C11).
// Owns: provider vtable open/close/addr_to_line over the row cache.
// Depends: re_sym_dwarf_priv.h, re_buf. No PDB parsers share this file.
#include "features/sym/re_sym_dwarf_priv.h"

#include "utils/sys/re_path.h"

#include <string.h>

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

static re_err_code_t dwarf_open(re_sym_provider_t *p, re_arena_t *a, const char *path,
                                uint64_t base) {
    re_dwarf_impl_t *im;
    re_file_t f;
    re_err_code_t c;
    if (!p || !p->impl || !a || !path || !path[0])
        return RE_E_USAGE;
    im = (re_dwarf_impl_t *)p->impl;
    if (im->open)
        (void)p->vt->close_module(p);
    im->arena = a;
    copy_name(im->path, sizeof(im->path), path);
    im->base = base;
    im->n_rows = 0;
    im->n_files = 0;
    c = re_file_open(path, a, &f);
    if (c != RE_OK)
        return c;
    c = re_dwarf_load_lines(im, f.whole);
    re_file_close(&f);
    if (c != RE_OK)
        return c;
    im->open = true;
    return RE_OK;
}

static re_err_code_t dwarf_close(re_sym_provider_t *p) {
    re_dwarf_impl_t *im;
    if (!p || !p->impl)
        return RE_E_USAGE;
    im = (re_dwarf_impl_t *)p->impl;
    im->open = false;
    im->n_rows = 0;
    im->rows = NULL;
    im->path[0] = 0;
    return RE_OK;
}

static re_err_code_t dwarf_resolve(re_sym_provider_t *p, const char *name, uint64_t *addr_out,
                                   re_dbg_symbol_t *info_out) {
    (void)p;
    (void)name;
    if (addr_out)
        *addr_out = 0;
    if (info_out)
        memset(info_out, 0, sizeof(*info_out));
    return RE_E_UNSUPPORTED;
}

static re_err_code_t dwarf_addr_sym(re_sym_provider_t *p, uint64_t addr,
                                    re_dbg_symbol_t *info_out) {
    (void)p;
    (void)addr;
    if (info_out)
        memset(info_out, 0, sizeof(*info_out));
    return RE_E_UNSUPPORTED;
}

static re_err_code_t dwarf_addr_line(re_sym_provider_t *p, uint64_t addr, re_sym_line_t *out) {
    re_dwarf_impl_t *im;
    size_t i;
    size_t best = (size_t)-1;
    uint64_t best_addr = 0;
    if (!p || !p->impl || !out)
        return RE_E_USAGE;
    im = (re_dwarf_impl_t *)p->impl;
    if (!im->open || !im->rows)
        return RE_E_USAGE;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < im->n_rows; i++) {
        if (im->rows[i].addr > addr)
            continue;
        if (best == (size_t)-1 || im->rows[i].addr >= best_addr) {
            best = i;
            best_addr = im->rows[i].addr;
        }
    }
    if (best == (size_t)-1)
        return RE_E_NOTFOUND;
    out->addr = im->rows[best].addr;
    out->line = im->rows[best].line;
    out->column = im->rows[best].column;
    if (im->rows[best].file_idx >= 1 && (size_t)(im->rows[best].file_idx - 1) < im->n_files)
        copy_name(out->file, sizeof(out->file), im->files[im->rows[best].file_idx - 1].name);
    return RE_OK;
}

static void dwarf_destroy(re_sym_provider_t *p) {
    if (p && p->vt && p->vt->close_module)
        (void)p->vt->close_module(p);
}

static const re_sym_provider_vtbl_t k_dwarf_vt = {"dwarf_line",  dwarf_open,     dwarf_close,
                                                  dwarf_resolve, dwarf_addr_sym, dwarf_addr_line,
                                                  dwarf_destroy};

re_sym_provider_t *re_sym_dwarf_create(re_arena_t *a) {
    re_sym_provider_t *p;
    re_dwarf_impl_t *im;
    if (!a)
        return NULL;
    p = (re_sym_provider_t *)re_arena_alloc(a, sizeof(*p));
    im = (re_dwarf_impl_t *)re_arena_alloc(a, sizeof(*im));
    if (!p || !im)
        return NULL;
    memset(im, 0, sizeof(*im));
    p->vt = &k_dwarf_vt;
    p->impl = im;
    return p;
}

re_err_code_t re_sym_line_from_file(re_arena_t *a, const char *path, uint64_t base, uint64_t addr,
                                    re_sym_line_t *out) {
    re_sym_provider_t *d;
    re_err_code_t c;
    if (!a || !path || !out)
        return RE_E_USAGE;
    d = re_sym_dwarf_create(a);
    if (!d)
        return RE_E_NOMEM;
    c = d->vt->open_module(d, a, path, base);
    if (c != RE_OK)
        return c;
    c = d->vt->addr_to_line(d, addr, out);
    (void)d->vt->close_module(d);
    return c;
}
