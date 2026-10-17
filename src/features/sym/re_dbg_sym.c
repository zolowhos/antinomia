// re_dbg_sym.c - symbol resolve v0 from PE exports and ELF dynsym.
// Module: feature/sym (C11).
// Owns: file-backed module:symbol lookup helpers for bp-style queries.
// Depends: re_dbg.h, re_sym_prov, re_buf, re_path. Chains PDB after exports.
#include "features/debug/re_dbg.h"
#include "features/sym/re_sym_prov.h"

#include "utils/mem/re_buf.h"
#include "utils/sys/re_path.h"
#include "utils/text/re_str.h"

#include <string.h>

static void copy_name(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    if (!dst || cap == 0)
        return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static bool cstr_eq(re_span_t s, const char *want) {
    size_t n = 0;
    if (!want)
        return false;
    while (want[n])
        n++;
    if (s.n != n)
        return false;
    return s.n == 0 || memcmp(s.p, want, n) == 0;
}

static bool mod_name_match(const char *path, const char *want) {
    const char *base;
    re_str_t b, w;
    size_t i;
    if (!path || !want || !want[0])
        return false;
    base = re_path_basename_ptr(path);
    b = re_str(base);
    w = re_str(want);
    if (re_str_ieq_cstr(b, want))
        return true;
    /* libc matches libc.so.6; kernel32 matches kernel32.dll */
    if (b.n > w.n && re_str_istarts_cstr(b, want)) {
        char c = b.p[w.n];
        return c == '.' || c == '-';
    }
    for (i = 0; i < b.n; i++) {
        if (b.p[i] != '.')
            continue;
        if (i == w.n && re_str_ieq_cstr(re_strn(b.p, i), want))
            return true;
        break;
    }
    return false;
}

typedef struct {
    uint8_t cls;
    uint64_t shoff;
    uint16_t shentsz;
    uint16_t shnum;
    uint64_t sym_off;
    uint64_t sym_size;
    uint64_t str_off;
    uint64_t str_size;
} elf_symtab_t;

static re_err_code_t elf_hdr(re_span_t file, elf_symtab_t *e) {
    memset(e, 0, sizeof(*e));
    if (file.n < 64 || file.p[0] != 0x7f || file.p[1] != 'E' || file.p[2] != 'L' ||
        file.p[3] != 'F')
        return RE_E_NOTBIN;
    e->cls = file.p[4];
    if (e->cls == 2) {
        if (!re_rd64(file, 40, &e->shoff) || !re_rd16(file, 58, &e->shentsz) ||
            !re_rd16(file, 60, &e->shnum))
            return RE_E_MALFORMED;
        return RE_OK;
    }
    if (e->cls == 1) {
        uint32_t sh32 = 0;
        if (!re_rd32(file, 32, &sh32) || !re_rd16(file, 46, &e->shentsz) ||
            !re_rd16(file, 48, &e->shnum))
            return RE_E_MALFORMED;
        e->shoff = sh32;
        return RE_OK;
    }
    return RE_E_UNSUPPORTED;
}

static bool elf_sh_read(re_span_t file, const elf_symtab_t *e, uint16_t i, uint32_t *shtype,
                        uint32_t *link, uint64_t *soff, uint64_t *ssize) {
    uint64_t off = e->shoff + (uint64_t)i * e->shentsz;
    if (e->cls == 2)
        return re_rd32(file, off + 4, shtype) && re_rd64(file, off + 24, soff) &&
               re_rd64(file, off + 32, ssize) && re_rd32(file, off + 40, link);
    {
        uint32_t o32 = 0, s32 = 0;
        if (!re_rd32(file, off + 4, shtype) || !re_rd32(file, off + 16, &o32) ||
            !re_rd32(file, off + 20, &s32) || !re_rd32(file, off + 24, link))
            return false;
        *soff = o32;
        *ssize = s32;
        return true;
    }
}

static re_err_code_t elf_find_symtab(re_span_t file, elf_symtab_t *e) {
    for (uint16_t i = 0; i < e->shnum; i++) {
        uint32_t shtype = 0, link = 0, dummy = 0;
        uint64_t soff = 0, ssize = 0, str_off = 0, str_sz = 0;
        if (!elf_sh_read(file, e, i, &shtype, &link, &soff, &ssize))
            return RE_E_MALFORMED;
        if (shtype != 11 && shtype != 2)
            continue;
        if (!elf_sh_read(file, e, (uint16_t)link, &dummy, &dummy, &str_off, &str_sz))
            return RE_E_MALFORMED;
        if (shtype == 11 || e->sym_off == 0) {
            e->sym_off = soff;
            e->sym_size = ssize;
            e->str_off = str_off;
            e->str_size = str_sz;
        }
        if (shtype == 11)
            return RE_OK;
    }
    return e->sym_off ? RE_OK : RE_E_NOTFOUND;
}

static re_err_code_t elf_scan_syms(re_span_t file, const elf_symtab_t *e, uint64_t base,
                                   const char *name, uint64_t *out) {
    size_t entsz = e->cls == 2 ? 24u : 16u;
    size_t n = (size_t)(e->sym_size / entsz);
    for (size_t i = 0; i < n; i++) {
        uint64_t ent = e->sym_off + (uint64_t)i * entsz;
        uint32_t st_name = 0;
        uint64_t st_value = 0;
        uint16_t st_shndx = 0;
        re_span_t nm;
        if (e->cls == 2) {
            if (!re_rd32(file, ent, &st_name) || !re_rd16(file, ent + 6, &st_shndx) ||
                !re_rd64(file, ent + 8, &st_value))
                return RE_E_MALFORMED;
        } else {
            uint32_t v32 = 0;
            if (!re_rd32(file, ent, &st_name) || !re_rd32(file, ent + 4, &v32) ||
                !re_rd16(file, ent + 14, &st_shndx))
                return RE_E_MALFORMED;
            st_value = v32;
        }
        if (st_shndx == 0 || st_name == 0)
            continue;
        if (!re_rd_cstr(file, e->str_off + st_name, e->str_size, &nm))
            continue;
        if (!cstr_eq(nm, name))
            continue;
        *out = base + st_value;
        return RE_OK;
    }
    return RE_E_NOTFOUND;
}

static re_err_code_t elf_resolve(re_span_t file, uint64_t base, const char *name, uint64_t *out) {
    elf_symtab_t e;
    re_err_code_t c = elf_hdr(file, &e);
    if (c != RE_OK)
        return c;
    c = elf_find_symtab(file, &e);
    if (c != RE_OK)
        return c;
    return elf_scan_syms(file, &e, base, name, out);
}

static uint32_t pe_rva_to_off(re_span_t file, uint32_t rva, uint32_t sect_off, uint16_t nsect,
                              uint16_t opt_sz) {
    uint32_t i;
    (void)opt_sz;
    for (i = 0; i < nsect; i++) {
        uint32_t off = sect_off + i * 40u;
        uint32_t vsize = 0, vaddr = 0, rawsz = 0, rawptr = 0;
        if (!re_rd32(file, off + 8, &vsize) || !re_rd32(file, off + 12, &vaddr) ||
            !re_rd32(file, off + 16, &rawsz) || !re_rd32(file, off + 20, &rawptr))
            return 0;
        if (rva >= vaddr && rva < vaddr + (vsize > rawsz ? vsize : rawsz))
            return rawptr + (rva - vaddr);
    }
    return 0;
}

static re_err_code_t pe_resolve(re_span_t file, uint64_t base, const char *name, uint64_t *out) {
    uint32_t e_lfanew = 0, exp_rva = 0, exp_off = 0;
    uint16_t magic = 0, nsect = 0, opt_sz = 0;
    uint32_t sect_off = 0, names = 0, ords = 0, funcs = 0, nnames = 0;
    uint32_t names_off, ords_off, funcs_off;
    if (file.n < 0x40 || file.p[0] != 'M' || file.p[1] != 'Z')
        return RE_E_NOTBIN;
    if (!re_rd32(file, 0x3c, &e_lfanew) || e_lfanew + 0x18 >= file.n)
        return RE_E_MALFORMED;
    if (file.p[e_lfanew] != 'P' || file.p[e_lfanew + 1] != 'E')
        return RE_E_MALFORMED;
    if (!re_rd16(file, e_lfanew + 6, &nsect) || !re_rd16(file, e_lfanew + 20, &opt_sz))
        return RE_E_MALFORMED;
    if (!re_rd16(file, e_lfanew + 24, &magic))
        return RE_E_MALFORMED;
    {
        uint64_t dd = e_lfanew + 24u + (magic == 0x20b ? 112u : 96u);
        if (!re_rd32(file, dd, &exp_rva) || exp_rva == 0)
            return RE_E_NOTFOUND;
    }
    sect_off = e_lfanew + 24u + opt_sz;
    exp_off = pe_rva_to_off(file, exp_rva, sect_off, nsect, opt_sz);
    if (!exp_off || !re_rd32(file, exp_off + 24, &nnames) || !re_rd32(file, exp_off + 28, &funcs) ||
        !re_rd32(file, exp_off + 32, &names) || !re_rd32(file, exp_off + 36, &ords))
        return RE_E_MALFORMED;
    names_off = pe_rva_to_off(file, names, sect_off, nsect, opt_sz);
    ords_off = pe_rva_to_off(file, ords, sect_off, nsect, opt_sz);
    funcs_off = pe_rva_to_off(file, funcs, sect_off, nsect, opt_sz);
    if (!names_off || !ords_off || !funcs_off)
        return RE_E_MALFORMED;
    for (uint32_t i = 0; i < nnames; i++) {
        uint32_t name_rva = 0, name_off = 0, func_rva = 0;
        uint16_t ord = 0;
        re_span_t nm;
        if (!re_rd32(file, names_off + i * 4u, &name_rva) ||
            !re_rd16(file, ords_off + i * 2u, &ord))
            return RE_E_MALFORMED;
        name_off = pe_rva_to_off(file, name_rva, sect_off, nsect, opt_sz);
        if (!name_off || !re_rd_cstr(file, name_off, 256, &nm) || !cstr_eq(nm, name))
            continue;
        if (!re_rd32(file, funcs_off + (uint32_t)ord * 4u, &func_rva))
            return RE_E_MALFORMED;
        *out = base + func_rva;
        return RE_OK;
    }
    return RE_E_NOTFOUND;
}

re_err_code_t re_dbg_sym_resolve_file(re_arena_t *a, const char *path, uint64_t base,
                                      const char *name, uint64_t *addr_out) {
    re_file_t f;
    re_err_code_t c;
    if (!path || !name || !addr_out)
        return RE_E_USAGE;
    *addr_out = 0;
    c = re_file_open(path, a, &f);
    if (c != RE_OK)
        return c;
    if (f.whole.n >= 4 && f.whole.p[0] == 0x7f && f.whole.p[1] == 'E')
        c = elf_resolve(f.whole, base, name, addr_out);
    else if (f.whole.n >= 2 && f.whole.p[0] == 'M' && f.whole.p[1] == 'Z')
        c = pe_resolve(f.whole, base, name, addr_out);
    else
        c = RE_E_NOTBIN;
    re_file_close(&f);
    return c;
}

static void copy_span_name(char *dst, size_t cap, re_span_t nm) {
    size_t i = 0;
    if (!dst || cap == 0)
        return;
    while (i + 1 < cap && i < nm.n && nm.p[i]) {
        dst[i] = (char)nm.p[i];
        i++;
    }
    dst[i] = 0;
}

static re_err_code_t elf_nearest(re_span_t file, uint64_t base, uint64_t addr, char *name_out,
                                 size_t name_cap, uint64_t *sym_addr_out) {
    elf_symtab_t e;
    size_t entsz, n, i;
    uint64_t best = 0;
    re_span_t best_nm;
    re_err_code_t c = elf_hdr(file, &e);
    memset(&best_nm, 0, sizeof(best_nm));
    if (c != RE_OK)
        return c;
    c = elf_find_symtab(file, &e);
    if (c != RE_OK)
        return c;
    entsz = e.cls == 2 ? 24u : 16u;
    n = (size_t)(e.sym_size / entsz);
    for (i = 0; i < n; i++) {
        uint64_t ent = e.sym_off + (uint64_t)i * entsz;
        uint32_t st_name = 0;
        uint64_t st_value = 0;
        uint16_t st_shndx = 0;
        uint64_t abs;
        re_span_t nm;
        if (e.cls == 2) {
            if (!re_rd32(file, ent, &st_name) || !re_rd16(file, ent + 6, &st_shndx) ||
                !re_rd64(file, ent + 8, &st_value))
                continue;
        } else {
            uint32_t v32 = 0;
            if (!re_rd32(file, ent, &st_name) || !re_rd32(file, ent + 4, &v32) ||
                !re_rd16(file, ent + 14, &st_shndx))
                continue;
            st_value = v32;
        }
        if (st_shndx == 0 || st_name == 0)
            continue;
        abs = base + st_value;
        if (abs > addr || abs < best)
            continue;
        if (!re_rd_cstr(file, e.str_off + st_name, e.str_size, &nm) || nm.n == 0)
            continue;
        best = abs;
        best_nm = nm;
    }
    if (best == 0)
        return RE_E_NOTFOUND;
    if (sym_addr_out)
        *sym_addr_out = best;
    copy_span_name(name_out, name_cap, best_nm);
    return RE_OK;
}

static re_err_code_t pe_nearest(re_span_t file, uint64_t base, uint64_t addr, char *name_out,
                                size_t name_cap, uint64_t *sym_addr_out) {
    uint32_t e_lfanew = 0, exp_rva = 0, exp_off = 0;
    uint16_t magic = 0, nsect = 0, opt_sz = 0;
    uint32_t sect_off = 0, names = 0, ords = 0, funcs = 0, nnames = 0;
    uint32_t names_off, ords_off, funcs_off, i;
    uint64_t best = 0;
    re_span_t best_nm;
    memset(&best_nm, 0, sizeof(best_nm));
    if (file.n < 0x40 || file.p[0] != 'M' || file.p[1] != 'Z')
        return RE_E_NOTBIN;
    if (!re_rd32(file, 0x3c, &e_lfanew) || e_lfanew + 0x18 >= file.n)
        return RE_E_MALFORMED;
    if (file.p[e_lfanew] != 'P' || file.p[e_lfanew + 1] != 'E')
        return RE_E_MALFORMED;
    if (!re_rd16(file, e_lfanew + 6, &nsect) || !re_rd16(file, e_lfanew + 20, &opt_sz) ||
        !re_rd16(file, e_lfanew + 24, &magic))
        return RE_E_MALFORMED;
    {
        uint64_t dd = e_lfanew + 24u + (magic == 0x20b ? 112u : 96u);
        if (!re_rd32(file, dd, &exp_rva) || exp_rva == 0)
            return RE_E_NOTFOUND;
    }
    sect_off = e_lfanew + 24u + opt_sz;
    exp_off = pe_rva_to_off(file, exp_rva, sect_off, nsect, opt_sz);
    if (!exp_off || !re_rd32(file, exp_off + 24, &nnames) || !re_rd32(file, exp_off + 28, &funcs) ||
        !re_rd32(file, exp_off + 32, &names) || !re_rd32(file, exp_off + 36, &ords))
        return RE_E_MALFORMED;
    names_off = pe_rva_to_off(file, names, sect_off, nsect, opt_sz);
    ords_off = pe_rva_to_off(file, ords, sect_off, nsect, opt_sz);
    funcs_off = pe_rva_to_off(file, funcs, sect_off, nsect, opt_sz);
    if (!names_off || !ords_off || !funcs_off)
        return RE_E_MALFORMED;
    for (i = 0; i < nnames; i++) {
        uint32_t name_rva = 0, name_off = 0, func_rva = 0;
        uint16_t ord = 0;
        uint64_t abs;
        re_span_t nm;
        if (!re_rd32(file, names_off + i * 4u, &name_rva) ||
            !re_rd16(file, ords_off + i * 2u, &ord))
            continue;
        name_off = pe_rva_to_off(file, name_rva, sect_off, nsect, opt_sz);
        if (!name_off || !re_rd_cstr(file, name_off, 256, &nm) || nm.n == 0)
            continue;
        if (!re_rd32(file, funcs_off + (uint32_t)ord * 4u, &func_rva))
            continue;
        abs = base + func_rva;
        if (abs > addr || abs < best)
            continue;
        best = abs;
        best_nm = nm;
    }
    if (best == 0)
        return RE_E_NOTFOUND;
    if (sym_addr_out)
        *sym_addr_out = best;
    copy_span_name(name_out, name_cap, best_nm);
    return RE_OK;
}

re_err_code_t re_dbg_sym_nearest_file(re_arena_t *a, const char *path, uint64_t base, uint64_t addr,
                                      char *name_out, size_t name_cap, uint64_t *sym_addr_out) {
    re_file_t f;
    re_err_code_t c;
    if (name_out && name_cap)
        name_out[0] = 0;
    if (sym_addr_out)
        *sym_addr_out = 0;
    if (!path || !name_out || name_cap == 0)
        return RE_E_USAGE;
    c = re_file_open(path, a, &f);
    if (c != RE_OK)
        return c;
    if (f.whole.n >= 4 && f.whole.p[0] == 0x7f && f.whole.p[1] == 'E')
        c = elf_nearest(f.whole, base, addr, name_out, name_cap, sym_addr_out);
    else if (f.whole.n >= 2 && f.whole.p[0] == 'M' && f.whole.p[1] == 'Z')
        c = pe_nearest(f.whole, base, addr, name_out, name_cap, sym_addr_out);
    else
        c = RE_E_NOTBIN;
    re_file_close(&f);
    return c;
}

static re_err_code_t try_prov(re_sym_provider_t *p, re_arena_t *a, const char *path, uint64_t base,
                              const char *sym, uint64_t *addr, re_dbg_symbol_t *info) {
    re_err_code_t c;
    if (!p || !p->vt)
        return RE_E_UNSUPPORTED;
    c = p->vt->open_module(p, a, path, base);
    if (c != RE_OK)
        return c;
    c = p->vt->resolve(p, sym, addr, info);
    (void)p->vt->close_module(p);
    return c;
}

static re_err_code_t resolve_one(re_dbg_session_t *s, const re_dbg_module_t *m, const char *sym,
                                 re_sym_provider_t *exp, re_sym_provider_t *pdb, uint64_t *addr_out,
                                 re_dbg_symbol_t *info_out) {
    uint64_t addr = 0;
    re_err_code_t c = try_prov(exp, &s->arena, m->path, m->base, sym, &addr, info_out);
    if (c == RE_OK) {
        if (addr_out)
            *addr_out = addr;
        return RE_OK;
    }
    c = try_prov(pdb, &s->arena, m->path, m->base, sym, &addr, info_out);
    if (c == RE_OK) {
        if (addr_out)
            *addr_out = addr;
        return RE_OK;
    }
    c = re_dbg_sym_resolve_file(&s->arena, m->path, m->base, sym, &addr);
    if (c != RE_OK)
        return c;
    if (addr_out)
        *addr_out = addr;
    if (info_out) {
        info_out->addr = addr;
        copy_name(info_out->name, sizeof(info_out->name), sym);
        copy_name(info_out->module, sizeof(info_out->module), re_path_basename_ptr(m->path));
    }
    return RE_OK;
}

re_err_code_t re_dbg_session_resolve(re_dbg_session_t *s, const char *query, uint64_t *addr_out,
                                     re_dbg_symbol_t *info_out) {
    static re_dbg_module_t mods[RE_DBG_MAX_MODULES];
    size_t n = 0, i;
    char mod[64];
    const char *sym;
    const char *colon;
    re_err_code_t c;
    re_sym_provider_t *exp;
    re_sym_provider_t *pdb;
    if (addr_out)
        *addr_out = 0;
    if (info_out)
        memset(info_out, 0, sizeof(*info_out));
    if (!s || !query || !query[0])
        return RE_E_USAGE;
    colon = strchr(query, ':');
    if (colon && colon != query) {
        size_t mn = (size_t)(colon - query);
        if (mn >= sizeof(mod))
            return RE_E_USAGE;
        memcpy(mod, query, mn);
        mod[mn] = 0;
        sym = colon + 1;
    } else {
        mod[0] = 0;
        sym = query;
    }
    if (!sym[0])
        return RE_E_USAGE;
    c = re_dbg_session_enum_modules(s, mods, RE_DBG_MAX_MODULES, &n);
    if (c != RE_OK)
        return c;
    exp = re_sym_export_create(&s->arena);
    pdb = re_sym_pdb_create(&s->arena);
    for (i = 0; i < n; i++) {
        if (mod[0] && !mod_name_match(mods[i].path, mod))
            continue;
        if (resolve_one(s, &mods[i], sym, exp, pdb, addr_out, info_out) == RE_OK)
            return RE_OK;
    }
    return RE_E_NOTFOUND;
}
