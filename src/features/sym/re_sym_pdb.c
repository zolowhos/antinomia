// re_sym_pdb.c - Windows PDB symbols via dbghelp, wrapped as a symbol provider.
// Module: feature/sym (C11).
// Owns: dbghelp Sym* calls only; never shares parsers with DWARF.
// Depends: re_sym_prov.h. Links dbghelp on Win32; stub elsewhere.
#include "features/sym/re_sym_prov.h"

#include "utils/sys/re_path.h"

#include <string.h>

#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
/* windows.h must precede dbghelp.h (SortIncludes would reverse them). */
/* clang-format off */
#    include <windows.h>
#    include <dbghelp.h>
/* clang-format on */
#endif

typedef struct {
    re_arena_t *arena;
    char path[RE_SYM_PATH_MAX];
    uint64_t base;
    bool open;
#if defined(_WIN32)
    HANDLE process;
    DWORD64 mod_base;
#endif
} pdb_impl_t;

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

#if defined(_WIN32)

static re_err_code_t pdb_open(re_sym_provider_t *p, re_arena_t *a, const char *path,
                              uint64_t base) {
    pdb_impl_t *im;
    DWORD64 loaded;
    if (!p || !p->impl || !a || !path || !path[0])
        return RE_E_USAGE;
    im = (pdb_impl_t *)p->impl;
    if (im->open)
        (void)p->vt->close_module(p);
    im->arena = a;
    copy_name(im->path, sizeof(im->path), path);
    im->base = base;
    /* Reject missing images before dbghelp; DEFERRED_LOADS can otherwise succeed. */
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
        return RE_E_IO;
    im->process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    if (!SymInitialize(im->process, NULL, FALSE))
        return RE_E_IO;
    loaded = SymLoadModuleEx(im->process, NULL, path, NULL, (DWORD64)base, 0, NULL, 0);
    if (!loaded) {
        SymCleanup(im->process);
        return RE_E_IO;
    }
    im->mod_base = loaded;
    im->open = true;
    return RE_OK;
}

static re_err_code_t pdb_close(re_sym_provider_t *p) {
    pdb_impl_t *im;
    if (!p || !p->impl)
        return RE_E_USAGE;
    im = (pdb_impl_t *)p->impl;
    if (im->open && im->process) {
        if (im->mod_base)
            SymUnloadModule64(im->process, im->mod_base);
        SymCleanup(im->process);
    }
    im->open = false;
    im->mod_base = 0;
    im->path[0] = 0;
    return RE_OK;
}

static re_err_code_t pdb_resolve(re_sym_provider_t *p, const char *name, uint64_t *addr_out,
                                 re_dbg_symbol_t *info_out) {
    pdb_impl_t *im;
    char buf[sizeof(SYMBOL_INFO) + RE_SYM_NAME_MAX];
    SYMBOL_INFO *sym;
    if (!p || !p->impl || !name || !name[0])
        return RE_E_USAGE;
    im = (pdb_impl_t *)p->impl;
    if (!im->open)
        return RE_E_USAGE;
    if (addr_out)
        *addr_out = 0;
    if (info_out)
        memset(info_out, 0, sizeof(*info_out));
    memset(buf, 0, sizeof(buf));
    sym = (SYMBOL_INFO *)buf;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = RE_SYM_NAME_MAX;
    if (!SymFromName(im->process, name, sym))
        return RE_E_NOTFOUND;
    if (addr_out)
        *addr_out = (uint64_t)sym->Address;
    if (info_out) {
        info_out->addr = (uint64_t)sym->Address;
        copy_name(info_out->name, sizeof(info_out->name), sym->Name);
        copy_name(info_out->module, sizeof(info_out->module), re_path_basename_ptr(im->path));
    }
    return RE_OK;
}

static re_err_code_t pdb_addr_sym(re_sym_provider_t *p, uint64_t addr, re_dbg_symbol_t *info_out) {
    pdb_impl_t *im;
    char buf[sizeof(SYMBOL_INFO) + RE_SYM_NAME_MAX];
    SYMBOL_INFO *sym;
    DWORD64 disp = 0;
    if (!p || !p->impl || !info_out)
        return RE_E_USAGE;
    im = (pdb_impl_t *)p->impl;
    if (!im->open)
        return RE_E_USAGE;
    memset(info_out, 0, sizeof(*info_out));
    memset(buf, 0, sizeof(buf));
    sym = (SYMBOL_INFO *)buf;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = RE_SYM_NAME_MAX;
    if (!SymFromAddr(im->process, (DWORD64)addr, &disp, sym))
        return RE_E_NOTFOUND;
    info_out->addr = (uint64_t)sym->Address;
    copy_name(info_out->name, sizeof(info_out->name), sym->Name);
    copy_name(info_out->module, sizeof(info_out->module), re_path_basename_ptr(im->path));
    return RE_OK;
}

static re_err_code_t pdb_addr_line(re_sym_provider_t *p, uint64_t addr, re_sym_line_t *out) {
    pdb_impl_t *im;
    IMAGEHLP_LINE64 line;
    DWORD disp = 0;
    if (!p || !p->impl || !out)
        return RE_E_USAGE;
    im = (pdb_impl_t *)p->impl;
    if (!im->open)
        return RE_E_USAGE;
    memset(out, 0, sizeof(*out));
    memset(&line, 0, sizeof(line));
    line.SizeOfStruct = sizeof(line);
    if (!SymGetLineFromAddr64(im->process, (DWORD64)addr, &disp, &line))
        return RE_E_NOTFOUND;
    out->addr = addr;
    out->line = (uint32_t)line.LineNumber;
    out->column = 0;
    copy_name(out->file, sizeof(out->file), line.FileName ? line.FileName : "");
    return RE_OK;
}

#else /* !_WIN32 */

static re_err_code_t pdb_open(re_sym_provider_t *p, re_arena_t *a, const char *path,
                              uint64_t base) {
    pdb_impl_t *im;
    if (!p || !p->impl || !a || !path || !path[0])
        return RE_E_USAGE;
    im = (pdb_impl_t *)p->impl;
    im->arena = a;
    copy_name(im->path, sizeof(im->path), path);
    im->base = base;
    im->open = true;
    return RE_E_UNSUPPORTED; /* dbghelp is Windows-only */
}

static re_err_code_t pdb_close(re_sym_provider_t *p) {
    pdb_impl_t *im;
    if (!p || !p->impl)
        return RE_E_USAGE;
    im = (pdb_impl_t *)p->impl;
    im->open = false;
    im->path[0] = 0;
    return RE_OK;
}

static re_err_code_t pdb_resolve(re_sym_provider_t *p, const char *name, uint64_t *addr_out,
                                 re_dbg_symbol_t *info_out) {
    (void)p;
    (void)name;
    if (addr_out)
        *addr_out = 0;
    if (info_out)
        memset(info_out, 0, sizeof(*info_out));
    return RE_E_UNSUPPORTED;
}

static re_err_code_t pdb_addr_sym(re_sym_provider_t *p, uint64_t addr, re_dbg_symbol_t *info_out) {
    (void)p;
    (void)addr;
    if (info_out)
        memset(info_out, 0, sizeof(*info_out));
    return RE_E_UNSUPPORTED;
}

static re_err_code_t pdb_addr_line(re_sym_provider_t *p, uint64_t addr, re_sym_line_t *out) {
    (void)p;
    (void)addr;
    if (out)
        memset(out, 0, sizeof(*out));
    return RE_E_UNSUPPORTED;
}

#endif

static void pdb_destroy(re_sym_provider_t *p) {
    if (p && p->vt && p->vt->close_module)
        (void)p->vt->close_module(p);
}

static const re_sym_provider_vtbl_t k_pdb_vt = {
    "pdb_dbghelp", pdb_open, pdb_close, pdb_resolve, pdb_addr_sym, pdb_addr_line, pdb_destroy};

re_sym_provider_t *re_sym_pdb_create(re_arena_t *a) {
    re_sym_provider_t *p;
    pdb_impl_t *im;
    if (!a)
        return NULL;
    p = (re_sym_provider_t *)re_arena_alloc(a, sizeof(*p));
    im = (pdb_impl_t *)re_arena_alloc(a, sizeof(*im));
    if (!p || !im)
        return NULL;
    memset(im, 0, sizeof(*im));
    p->vt = &k_pdb_vt;
    p->impl = im;
    return p;
}
