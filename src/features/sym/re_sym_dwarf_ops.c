// re_sym_dwarf_ops.c - DWARF line-program opcode handlers (v2-v4).
// Module: feature/sym (C11).
// Owns: standard/special/extended opcode steps that emit line rows.
// Depends: re_sym_dwarf_priv.h. Keeps the runner under the function-size gate.
#include "features/sym/re_sym_dwarf_priv.h"

#include <string.h>

bool re_dwarf_uleb(re_span_t s, size_t *off, uint64_t *out) {
    uint64_t r = 0;
    unsigned shift = 0;
    while (*off < s.n) {
        uint8_t b = s.p[(*off)++];
        r |= (uint64_t)(b & 0x7fu) << shift;
        if ((b & 0x80u) == 0) {
            *out = r;
            return true;
        }
        shift += 7;
        if (shift > 63)
            return false;
    }
    return false;
}

bool re_dwarf_sleb(re_span_t s, size_t *off, int64_t *out) {
    uint64_t r = 0;
    unsigned shift = 0;
    uint8_t b = 0;
    while (*off < s.n) {
        b = s.p[(*off)++];
        r |= (uint64_t)(b & 0x7fu) << shift;
        shift += 7;
        if ((b & 0x80u) == 0)
            break;
        if (shift > 63)
            return false;
    }
    if (shift < 64 && (b & 0x40u))
        r |= (~(uint64_t)0) << shift;
    *out = (int64_t)r;
    return true;
}

bool re_dwarf_add_row(re_dwarf_impl_t *im, uint64_t addr, uint32_t line, uint32_t column,
                      uint16_t file_idx) {
    re_dwarf_row_t *r;
    if (im->n_rows >= RE_DWARF_MAX_ROWS)
        return false;
    r = &im->rows[im->n_rows++];
    r->addr = addr;
    r->line = line;
    r->column = column;
    r->file_idx = file_idx;
    return true;
}

re_err_code_t re_dwarf_op_extended(re_dwarf_impl_t *im, re_span_t unit, size_t *off,
                                   re_dwarf_regs_t *rg, uint8_t default_is_stmt) {
    uint64_t len = 0;
    size_t end;
    uint8_t ext = 0;
    if (!re_dwarf_uleb(unit, off, &len))
        return RE_E_MALFORMED;
    end = *off + (size_t)len;
    if (end > unit.n || len == 0)
        return RE_E_MALFORMED;
    if (!re_rd8(unit, (*off)++, &ext))
        return RE_E_MALFORMED;
    if (ext == 1) {
        if (!re_dwarf_add_row(im, im->base + rg->address, rg->line, rg->column, rg->file_idx))
            return RE_E_RANGE;
        rg->address = 0;
        rg->op_index = 0;
        rg->file_idx = 1;
        rg->line = 1;
        rg->column = 0;
        rg->is_stmt = default_is_stmt != 0;
    } else if (ext == 2) {
        if (len >= 9) {
            if (!re_rd64(unit, *off, &rg->address))
                return RE_E_MALFORMED;
        } else if (len >= 5) {
            uint32_t a32 = 0;
            if (!re_rd32(unit, *off, &a32))
                return RE_E_MALFORMED;
            rg->address = a32;
        }
        rg->op_index = 0;
    }
    *off = end;
    return RE_OK;
}

static void advance_pc(re_dwarf_regs_t *rg, const re_dwarf_hdr_t *h, uint64_t op_adv) {
    rg->address += ((op_adv + rg->op_index) / h->max_ops) * h->min_inst;
    rg->op_index = (uint8_t)((op_adv + rg->op_index) % h->max_ops);
}

re_err_code_t re_dwarf_op_standard(re_dwarf_impl_t *im, re_span_t unit, size_t *off,
                                   re_dwarf_regs_t *rg, const re_dwarf_hdr_t *h, uint8_t op) {
    size_t i;
    uint64_t u = 0;
    int64_t d = 0;
    if (op == 1)
        return re_dwarf_add_row(im, im->base + rg->address, rg->line, rg->column, rg->file_idx)
                   ? RE_OK
                   : RE_E_RANGE;
    if (op == 2) {
        if (!re_dwarf_uleb(unit, off, &u))
            return RE_E_MALFORMED;
        advance_pc(rg, h, u);
        return RE_OK;
    }
    if (op == 3) {
        if (!re_dwarf_sleb(unit, off, &d))
            return RE_E_MALFORMED;
        rg->line = (uint32_t)((int64_t)rg->line + d);
        return RE_OK;
    }
    if (op == 4) {
        if (!re_dwarf_uleb(unit, off, &u))
            return RE_E_MALFORMED;
        rg->file_idx = (uint16_t)u;
        return RE_OK;
    }
    if (op == 5) {
        if (!re_dwarf_uleb(unit, off, &u))
            return RE_E_MALFORMED;
        rg->column = (uint32_t)u;
        return RE_OK;
    }
    if (op == 6) {
        rg->is_stmt = !rg->is_stmt;
        return RE_OK;
    }
    if (op == 8) {
        advance_pc(rg, h, (255u - h->opcode_base) / h->line_range);
        return RE_OK;
    }
    if (op == 9) {
        uint16_t fixed = 0;
        if (!re_rd16(unit, *off, &fixed))
            return RE_E_MALFORMED;
        *off += 2;
        rg->address += fixed;
        rg->op_index = 0;
        return RE_OK;
    }
    if (op == 7)
        return RE_OK;
    for (i = 0; i < unit.p[h->std_op_lens_off + (op - 1u)]; i++) {
        if (!re_dwarf_uleb(unit, off, &u))
            return RE_E_MALFORMED;
    }
    return RE_OK;
}

re_err_code_t re_dwarf_op_special(re_dwarf_impl_t *im, re_dwarf_regs_t *rg, const re_dwarf_hdr_t *h,
                                  uint8_t op) {
    uint8_t adj = (uint8_t)(op - h->opcode_base);
    int8_t lb = (int8_t)h->line_base;
    uint64_t op_adv = adj / h->line_range;
    int32_t line_inc = (int32_t)lb + (int32_t)(adj % h->line_range);
    rg->address += ((op_adv + rg->op_index) / h->max_ops) * h->min_inst;
    rg->op_index = (uint8_t)((op_adv + rg->op_index) % h->max_ops);
    rg->line = (uint32_t)((int32_t)rg->line + line_inc);
    if (!re_dwarf_add_row(im, im->base + rg->address, rg->line, rg->column, rg->file_idx))
        return RE_E_RANGE;
    return RE_OK;
}
