// re_patch.h - live session patch list and PE/ELF write-back.
// Module: feature/patch (C11).
// Owns: in-memory patch records, assemble-apply, and file write-back.
// Depends: re_err, re_arena. Session wiring via re_dbg_session_t.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#include "utils/mem/re_arena.h"
#include "utils/sys/re_err.h"

#include "features/patch/re_asm.h"

#define RE_PATCH_MAX 64
#define RE_PATCH_PATH_MAX 260
#define RE_PATCH_HEX_MAX (RE_ASM_MAX_BYTES * 2 + 1)

typedef struct {
    uint64_t addr;
    uint32_t size;
    uint8_t orig[RE_ASM_MAX_BYTES];
    uint8_t bytes[RE_ASM_MAX_BYTES];
    char text[RE_ASM_TEXT_MAX];
    char module[RE_PATCH_PATH_MAX];
    uint64_t module_base;
    uint64_t rva;
    bool used;
} re_patch_entry_t;

typedef struct {
    re_patch_entry_t items[RE_PATCH_MAX];
    size_t n;
} re_patch_list_t;

struct re_dbg_session;

void re_patch_list_init(re_patch_list_t *pl);
void re_patch_list_clear(re_patch_list_t *pl);

/* Upsert by addr. Copies orig/bytes/text; module fields optional. */
re_err_code_t re_patch_list_add(re_patch_list_t *pl, uint64_t addr, const uint8_t *orig,
                                const uint8_t *bytes, uint32_t size, const char *text,
                                const char *module, uint64_t module_base, uint64_t rva);

/* Assemble text, write memory, record live list + user DB patch entry. */
re_err_code_t re_dbg_session_assemble(struct re_dbg_session *s, uint64_t addr, const char *text,
                                      size_t *n_out);

/* Map VA inside an image to a file offset (PE sections or ELF PT_LOAD). */
re_err_code_t re_patch_va_to_file_off(const char *path, uint64_t image_base, uint64_t va,
                                      uint64_t *file_off_out, uint64_t *rva_out);

/* Write live patches back to PE/ELF files. out_path NULL => "<mod>.patched". */
re_err_code_t re_dbg_session_patch_writeback(struct re_dbg_session *s, const char *out_path,
                                             re_arena_t *scratch, size_t *n_files_out);

#ifdef __cplusplus
}
#endif
