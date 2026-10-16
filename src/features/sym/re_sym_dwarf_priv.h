// re_sym_dwarf_priv.h - shared DWARF line-table types for the Linux provider.
// Module: feature/sym (C11).
// Owns: row/file caches and the line-program entry used by re_sym_dwarf*.c.
// Depends: re_sym_prov.h, re_buf. Private to the DWARF provider pair.
#pragma once

#include "features/sym/re_sym_prov.h"
#include "utils/mem/re_buf.h"

#define RE_DWARF_MAX_ROWS 4096
#define RE_DWARF_MAX_FILES 64
#define RE_DWARF_FILE_MAX 192

typedef struct {
    uint64_t addr;
    uint32_t line;
    uint32_t column;
    uint16_t file_idx;
} re_dwarf_row_t;

typedef struct {
    char name[RE_DWARF_FILE_MAX];
} re_dwarf_file_t;

typedef struct {
    re_arena_t *arena;
    char path[RE_SYM_PATH_MAX];
    uint64_t base;
    bool open;
    re_dwarf_row_t *rows;
    size_t n_rows;
    re_dwarf_file_t files[RE_DWARF_MAX_FILES];
    size_t n_files;
} re_dwarf_impl_t;

typedef struct {
    uint8_t min_inst;
    uint8_t max_ops;
    uint8_t default_is_stmt;
    uint8_t line_base;
    uint8_t line_range;
    uint8_t opcode_base;
    size_t std_op_lens_off;
} re_dwarf_hdr_t;

typedef struct {
    uint64_t address;
    uint32_t line;
    uint32_t column;
    uint16_t file_idx;
    uint8_t op_index;
    bool is_stmt;
} re_dwarf_regs_t;

bool re_dwarf_uleb(re_span_t s, size_t *off, uint64_t *out);
bool re_dwarf_sleb(re_span_t s, size_t *off, int64_t *out);
bool re_dwarf_add_row(re_dwarf_impl_t *im, uint64_t addr, uint32_t line, uint32_t column,
                      uint16_t file_idx);
re_err_code_t re_dwarf_op_extended(re_dwarf_impl_t *im, re_span_t unit, size_t *off,
                                   re_dwarf_regs_t *rg, uint8_t default_is_stmt);
re_err_code_t re_dwarf_op_standard(re_dwarf_impl_t *im, re_span_t unit, size_t *off,
                                   re_dwarf_regs_t *rg, const re_dwarf_hdr_t *h, uint8_t op);
re_err_code_t re_dwarf_op_special(re_dwarf_impl_t *im, re_dwarf_regs_t *rg, const re_dwarf_hdr_t *h,
                                  uint8_t op);

re_err_code_t re_dwarf_find_debug_line(re_span_t file, re_span_t *sec);
re_err_code_t re_dwarf_run_line_prog(re_dwarf_impl_t *im, re_span_t unit);
re_err_code_t re_dwarf_load_lines(re_dwarf_impl_t *im, re_span_t file);
