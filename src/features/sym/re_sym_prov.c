// re_sym_prov.c - session helpers that chain DWARF and PDB for source lines.
// Module: feature/sym (C11).
// Owns: re_dbg_session_source over the symbol-provider vtable.
// Depends: re_sym_prov.h. PDB and DWARF stay in their own translation units.
#include "features/sym/re_sym_prov.h"

#include <string.h>

static re_err_code_t try_provider_line(re_sym_provider_t *p, re_arena_t *a, const char *path,
                                       uint64_t base, uint64_t addr, re_sym_line_t *out) {
    re_err_code_t c;
    if (!p || !p->vt)
        return RE_E_UNSUPPORTED;
    c = p->vt->open_module(p, a, path, base);
    if (c != RE_OK)
        return c;
    c = p->vt->addr_to_line(p, addr, out);
    (void)p->vt->close_module(p);
    return c;
}

re_err_code_t re_dbg_session_source(re_dbg_session_t *s, uint64_t addr, re_sym_line_t *out) {
    static re_dbg_module_t mods[RE_DBG_MAX_MODULES];
    size_t n = 0, i;
    re_err_code_t c;
    re_sym_provider_t *dwarf;
    re_sym_provider_t *pdb;
    if (!s || !out)
        return RE_E_USAGE;
    memset(out, 0, sizeof(*out));
    c = re_dbg_session_enum_modules(s, mods, RE_DBG_MAX_MODULES, &n);
    if (c != RE_OK)
        return c;
    dwarf = re_sym_dwarf_create(&s->arena);
    pdb = re_sym_pdb_create(&s->arena);
    for (i = 0; i < n; i++) {
        if (mods[i].size && (addr < mods[i].base || addr >= mods[i].base + mods[i].size))
            continue;
        if (dwarf) {
            c = try_provider_line(dwarf, &s->arena, mods[i].path, mods[i].base, addr, out);
            if (c == RE_OK)
                return RE_OK;
        }
        if (pdb) {
            c = try_provider_line(pdb, &s->arena, mods[i].path, mods[i].base, addr, out);
            if (c == RE_OK)
                return RE_OK;
        }
    }
    return RE_E_NOTFOUND;
}
