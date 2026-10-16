// re_sym_prov.h - opaque symbol-provider vtable (PDB vs DWARF never share parsers).
// Module: feature/sym (C11).
// Owns: provider contract, line-info types, and session-facing resolve helpers.
// Depends: re_arena, re_err, re_dbg types. OS parsers live in separate .c files.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "features/debug/re_dbg.h"
#include "utils/mem/re_arena.h"
#include "utils/sys/re_err.h"

#define RE_SYM_PATH_MAX 260
#define RE_SYM_NAME_MAX 128

typedef struct {
    uint64_t addr;
    uint32_t line;
    uint32_t column;
    char file[RE_SYM_PATH_MAX];
} re_sym_line_t;

typedef struct re_sym_provider re_sym_provider_t;

typedef struct re_sym_provider_vtbl {
    const char *name;
    re_err_code_t (*open_module)(re_sym_provider_t *p, re_arena_t *a, const char *path,
                                 uint64_t base);
    re_err_code_t (*close_module)(re_sym_provider_t *p);
    re_err_code_t (*resolve)(re_sym_provider_t *p, const char *name, uint64_t *addr_out,
                             re_dbg_symbol_t *info_out);
    re_err_code_t (*addr_to_symbol)(re_sym_provider_t *p, uint64_t addr, re_dbg_symbol_t *info_out);
    re_err_code_t (*addr_to_line)(re_sym_provider_t *p, uint64_t addr, re_sym_line_t *out);
    void (*destroy)(re_sym_provider_t *p);
} re_sym_provider_vtbl_t;

struct re_sym_provider {
    const re_sym_provider_vtbl_t *vt;
    void *impl;
};

/* Export/dynsym provider (always available). PDB and DWARF are OS-specific. */
re_sym_provider_t *re_sym_export_create(re_arena_t *a);
re_sym_provider_t *re_sym_pdb_create(re_arena_t *a);
re_sym_provider_t *re_sym_dwarf_create(re_arena_t *a);

/* Session helpers: try export then OS symbol providers. */
re_err_code_t re_dbg_session_source(re_dbg_session_t *s, uint64_t addr, re_sym_line_t *out);
re_err_code_t re_sym_line_from_file(re_arena_t *a, const char *path, uint64_t base, uint64_t addr,
                                    re_sym_line_t *out);

#ifdef __cplusplus
}
#endif
