// re_dbg_db.h - per-module JSON persistence for breakpoints, labels, comments.
// Module: feature (C11).
// Owns: load-on-module and save-on-exit database for user annotations.
// Depends: re_arena, re_err. No OS debug APIs; file I/O only.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "utils/mem/re_arena.h"
#include "utils/sys/re_err.h"

#define RE_DBG_DB_MAX_BP 32
#define RE_DBG_DB_MAX_LABEL 32
#define RE_DBG_DB_MAX_COMMENT 32
#define RE_DBG_DB_NAME_MAX 64
#define RE_DBG_DB_TEXT_MAX 128
#define RE_DBG_DB_PATH_MAX 512

typedef struct {
    uint64_t rva;
    bool enabled;
} re_dbg_db_bp_t;

typedef struct {
    uint64_t rva;
    char name[RE_DBG_DB_NAME_MAX];
} re_dbg_db_label_t;

typedef struct {
    uint64_t rva;
    char text[RE_DBG_DB_TEXT_MAX];
} re_dbg_db_comment_t;

typedef struct {
    char module_path[RE_DBG_DB_PATH_MAX];
    char db_path[RE_DBG_DB_PATH_MAX];
    bool loaded;
    bool dirty;
    re_dbg_db_bp_t bps[RE_DBG_DB_MAX_BP];
    size_t n_bps;
    re_dbg_db_label_t labels[RE_DBG_DB_MAX_LABEL];
    size_t n_labels;
    re_dbg_db_comment_t comments[RE_DBG_DB_MAX_COMMENT];
    size_t n_comments;
} re_dbg_db_t;

void re_dbg_db_init(re_dbg_db_t *db);
void re_dbg_db_clear(re_dbg_db_t *db);

// Derive "<module>.antinomia.json" beside the module image.
re_err_code_t re_dbg_db_path_for(const char *module_path, char *out, size_t cap);

// Missing file is ok (empty db). Malformed JSON returns RE_E_MALFORMED.
re_err_code_t re_dbg_db_load(re_dbg_db_t *db, re_arena_t *scratch, const char *module_path);
re_err_code_t re_dbg_db_save(const re_dbg_db_t *db, re_arena_t *scratch);

re_err_code_t re_dbg_db_add_bp(re_dbg_db_t *db, uint64_t rva, bool enabled);
re_err_code_t re_dbg_db_add_label(re_dbg_db_t *db, uint64_t rva, const char *name);
re_err_code_t re_dbg_db_add_comment(re_dbg_db_t *db, uint64_t rva, const char *text);

#ifdef __cplusplus
}
#endif
