// re_sym_dwarf_line.c - ELF .debug_line locate and DWARF v2-v4 line runner.
// Module: feature/sym (C11).
// Owns: section find, header parse, and the opcode dispatch loop.
// Depends: re_sym_dwarf_priv.h. DWARF 5 returns UNSUPPORTED (use -gdwarf-4).
#include "features/sym/re_sym_dwarf_priv.h"

#include <string.h>

static bool skip_cstr(re_span_t s, size_t *off) {
    while (*off < s.n) {
        if (s.p[(*off)++] == 0)
            return true;
    }
    return false;
}

static bool read_cstr(re_span_t s, size_t *off, char *dst, size_t cap) {
    size_t n = 0;
    while (*off < s.n && s.p[*off] != 0) {
        if (n + 1u < cap)
            dst[n++] = (char)s.p[*off];
        (*off)++;
    }
    if (*off >= s.n)
        return false;
    (*off)++;
    if (dst && cap)
        dst[n] = 0;
    return true;
}

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

typedef struct {
    uint8_t cls;
    uint64_t shoff;
    uint16_t shentsz;
    uint16_t shnum;
    uint16_t shstrndx;
} elf_hdr_t;

static re_err_code_t elf_parse_hdr(re_span_t file, elf_hdr_t *e) {
    memset(e, 0, sizeof(*e));
    if (file.n < 64 || file.p[0] != 0x7f || file.p[1] != 'E' || file.p[2] != 'L' ||
        file.p[3] != 'F')
        return RE_E_NOTBIN;
    e->cls = file.p[4];
    if (e->cls == 2) {
        if (!re_rd64(file, 40, &e->shoff) || !re_rd16(file, 58, &e->shentsz) ||
            !re_rd16(file, 60, &e->shnum) || !re_rd16(file, 62, &e->shstrndx))
            return RE_E_MALFORMED;
        return RE_OK;
    }
    if (e->cls == 1) {
        uint32_t sh32 = 0;
        if (!re_rd32(file, 32, &sh32) || !re_rd16(file, 46, &e->shentsz) ||
            !re_rd16(file, 48, &e->shnum) || !re_rd16(file, 50, &e->shstrndx))
            return RE_E_MALFORMED;
        e->shoff = sh32;
        return RE_OK;
    }
    return RE_E_UNSUPPORTED;
}

static bool elf_sh(re_span_t file, const elf_hdr_t *e, uint16_t i, uint32_t *name, uint32_t *type,
                   uint64_t *off, uint64_t *sz) {
    uint64_t o = e->shoff + (uint64_t)i * e->shentsz;
    if (e->cls == 2)
        return re_rd32(file, o, name) && re_rd32(file, o + 4, type) && re_rd64(file, o + 24, off) &&
               re_rd64(file, o + 32, sz);
    {
        uint32_t o32 = 0, s32 = 0;
        if (!re_rd32(file, o, name) || !re_rd32(file, o + 4, type) ||
            !re_rd32(file, o + 16, &o32) || !re_rd32(file, o + 20, &s32))
            return false;
        *off = o32;
        *sz = s32;
        return true;
    }
}

static bool elf_sh_name(re_span_t file, const elf_hdr_t *e, uint32_t name_off, re_span_t *out) {
    uint32_t dummy = 0, type = 0;
    uint64_t soff = 0, ssz = 0;
    uint64_t maxn;
    if (!elf_sh(file, e, e->shstrndx, &dummy, &type, &soff, &ssz))
        return false;
    maxn = name_off < ssz ? ssz - name_off : 0;
    return re_rd_cstr(file, soff + name_off, maxn, out);
}

re_err_code_t re_dwarf_find_debug_line(re_span_t file, re_span_t *sec) {
    elf_hdr_t e;
    re_err_code_t c = elf_parse_hdr(file, &e);
    uint16_t i;
    if (c != RE_OK)
        return c;
    *sec = re_span_none();
    for (i = 0; i < e.shnum; i++) {
        uint32_t name = 0, type = 0;
        uint64_t off = 0, sz = 0;
        re_span_t nm;
        if (!elf_sh(file, &e, i, &name, &type, &off, &sz))
            return RE_E_MALFORMED;
        if (!elf_sh_name(file, &e, name, &nm))
            continue;
        if (nm.n == 11 && memcmp(nm.p, ".debug_line", 11) == 0) {
            *sec = re_span_sub(file, off, sz);
            return re_span_valid(*sec) ? RE_OK : RE_E_MALFORMED;
        }
    }
    return RE_E_NOTFOUND;
}

static re_err_code_t read_prologue(re_span_t unit, size_t *off, uint16_t *version,
                                   re_dwarf_hdr_t *h) {
    uint64_t header_len = 0;
    bool dwarf64 = (*off == 12);
    if (!re_rd16(unit, *off, version))
        return RE_E_MALFORMED;
    *off += 2;
    if (*version < 2 || *version > 4)
        return RE_E_UNSUPPORTED;
    if (dwarf64) {
        if (!re_rd64(unit, *off, &header_len))
            return RE_E_MALFORMED;
        *off += 8;
    } else {
        uint32_t hl = 0;
        if (!re_rd32(unit, *off, &hl))
            return RE_E_MALFORMED;
        header_len = hl;
        *off += 4;
    }
    (void)header_len;
    memset(h, 0, sizeof(*h));
    h->max_ops = 1;
    if (*off >= unit.n || !re_rd8(unit, (*off)++, &h->min_inst))
        return RE_E_MALFORMED;
    if (*version >= 4) {
        if (!re_rd8(unit, (*off)++, &h->max_ops))
            return RE_E_MALFORMED;
        if (h->max_ops == 0)
            h->max_ops = 1;
    }
    if (!re_rd8(unit, (*off)++, &h->default_is_stmt) || !re_rd8(unit, (*off)++, &h->line_base) ||
        !re_rd8(unit, (*off)++, &h->line_range) || !re_rd8(unit, (*off)++, &h->opcode_base))
        return RE_E_MALFORMED;
    if (h->line_range == 0 || h->opcode_base == 0)
        return RE_E_MALFORMED;
    h->std_op_lens_off = *off;
    *off += (size_t)(h->opcode_base - 1u);
    return RE_OK;
}

static re_err_code_t read_files(re_dwarf_impl_t *im, re_span_t unit, size_t *off) {
    while (*off < unit.n && unit.p[*off] != 0) {
        if (!skip_cstr(unit, off))
            return RE_E_MALFORMED;
    }
    if (*off >= unit.n || unit.p[*off] != 0)
        return RE_E_MALFORMED;
    (*off)++;
    im->n_files = 0;
    while (*off < unit.n && unit.p[*off] != 0) {
        char name[RE_DWARF_FILE_MAX];
        uint64_t dir = 0, mtime = 0, fsize = 0;
        if (!read_cstr(unit, off, name, sizeof(name)))
            return RE_E_MALFORMED;
        if (!re_dwarf_uleb(unit, off, &dir) || !re_dwarf_uleb(unit, off, &mtime) ||
            !re_dwarf_uleb(unit, off, &fsize))
            return RE_E_MALFORMED;
        (void)dir;
        (void)mtime;
        (void)fsize;
        if (im->n_files < RE_DWARF_MAX_FILES) {
            copy_name(im->files[im->n_files].name, sizeof(im->files[0].name), name);
            im->n_files++;
        }
    }
    if (*off >= unit.n || unit.p[*off] != 0)
        return RE_E_MALFORMED;
    (*off)++;
    return RE_OK;
}

static re_err_code_t parse_header(re_dwarf_impl_t *im, re_span_t unit, size_t *off, size_t unit_end,
                                  re_dwarf_hdr_t *h, re_dwarf_regs_t *rg) {
    uint16_t version = 0;
    re_err_code_t c;
    (void)unit_end;
    c = read_prologue(unit, off, &version, h);
    if (c != RE_OK)
        return c;
    c = read_files(im, unit, off);
    if (c != RE_OK)
        return c;
    memset(rg, 0, sizeof(*rg));
    rg->line = 1;
    rg->file_idx = 1;
    rg->is_stmt = h->default_is_stmt != 0;
    return RE_OK;
}

re_err_code_t re_dwarf_run_line_prog(re_dwarf_impl_t *im, re_span_t unit) {
    size_t off = 0;
    uint32_t unit_len32 = 0;
    uint64_t unit_len = 0;
    size_t unit_end;
    re_dwarf_hdr_t h;
    re_dwarf_regs_t rg;
    re_err_code_t c;
    size_t rows_before = im->n_rows;

    if (!re_rd32(unit, 0, &unit_len32))
        return RE_E_MALFORMED;
    off = 4;
    if (unit_len32 == 0xffffffffu) {
        if (!re_rd64(unit, 4, &unit_len))
            return RE_E_MALFORMED;
        off = 12;
    } else {
        unit_len = unit_len32;
    }
    unit_end = off + (size_t)unit_len;
    if (unit_end > unit.n)
        return RE_E_MALFORMED;
    c = parse_header(im, unit, &off, unit_end, &h, &rg);
    if (c != RE_OK)
        return c;
    while (off < unit_end) {
        uint8_t op = 0;
        if (!re_rd8(unit, off++, &op))
            return RE_E_MALFORMED;
        if (op == 0)
            c = re_dwarf_op_extended(im, unit, &off, &rg, h.default_is_stmt);
        else if (op < h.opcode_base)
            c = re_dwarf_op_standard(im, unit, &off, &rg, &h, op);
        else
            c = re_dwarf_op_special(im, &rg, &h, op);
        if (c != RE_OK)
            return c;
    }
    return im->n_rows > rows_before ? RE_OK : RE_E_NOTFOUND;
}

re_err_code_t re_dwarf_load_lines(re_dwarf_impl_t *im, re_span_t file) {
    re_span_t sec;
    re_err_code_t c;
    size_t pos = 0;
    c = re_dwarf_find_debug_line(file, &sec);
    if (c != RE_OK)
        return c;
    im->rows =
        (re_dwarf_row_t *)re_arena_alloc(im->arena, sizeof(re_dwarf_row_t) * RE_DWARF_MAX_ROWS);
    if (!im->rows)
        return RE_E_NOMEM;
    im->n_rows = 0;
    while (pos + 4 < sec.n) {
        uint32_t len32 = 0;
        uint64_t len = 0;
        size_t unit_start = pos;
        size_t unit_size;
        re_span_t unit;
        if (!re_rd32(sec, pos, &len32))
            return RE_E_MALFORMED;
        if (len32 == 0xffffffffu) {
            if (!re_rd64(sec, pos + 4, &len))
                return RE_E_MALFORMED;
            unit_size = 12 + (size_t)len;
        } else {
            len = len32;
            unit_size = 4 + (size_t)len;
        }
        if (unit_start + unit_size > sec.n)
            return RE_E_MALFORMED;
        unit = re_span_sub(sec, unit_start, unit_size);
        c = re_dwarf_run_line_prog(im, unit);
        if (c != RE_OK && c != RE_E_NOTFOUND && c != RE_E_UNSUPPORTED)
            return c;
        pos = unit_start + unit_size;
        if (im->n_rows >= RE_DWARF_MAX_ROWS)
            break;
    }
    return im->n_rows ? RE_OK : RE_E_NOTFOUND;
}
