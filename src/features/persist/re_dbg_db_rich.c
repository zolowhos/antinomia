// re_dbg_db_rich.c - bookmarks, patch list, and function-boundary cache mutators.
// Module: feature/persist (C11).
// Owns: add/find helpers for the richer user-DB tables.
// Depends: re_dbg_db.h. Load/save stay in re_dbg_db.c.
#include "features/persist/re_dbg_db.h"

#include <string.h>

static void copy_cstr(char *dst, size_t cap, const char *src) {
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

re_err_code_t re_dbg_db_add_bookmark(re_dbg_db_t *db, uint64_t rva, const char *name) {
    size_t i;
    if (!db)
        return RE_E_USAGE;
    for (i = 0; i < db->n_bookmarks; i++) {
        if (db->bookmarks[i].rva == rva) {
            copy_cstr(db->bookmarks[i].name, sizeof(db->bookmarks[i].name), name ? name : "");
            db->dirty = true;
            return RE_OK;
        }
    }
    if (db->n_bookmarks >= RE_DBG_DB_MAX_BOOKMARK)
        return RE_E_RANGE;
    db->bookmarks[db->n_bookmarks].rva = rva;
    copy_cstr(db->bookmarks[db->n_bookmarks].name, sizeof(db->bookmarks[0].name), name ? name : "");
    db->n_bookmarks++;
    db->dirty = true;
    return RE_OK;
}

re_err_code_t re_dbg_db_add_patch(re_dbg_db_t *db, uint64_t rva, uint32_t size,
                                  const char *orig_hex, const char *patch_hex) {
    size_t i;
    if (!db || !orig_hex || !patch_hex)
        return RE_E_USAGE;
    for (i = 0; i < db->n_patches; i++) {
        if (db->patches[i].rva == rva) {
            db->patches[i].size = size;
            copy_cstr(db->patches[i].orig_hex, sizeof(db->patches[i].orig_hex), orig_hex);
            copy_cstr(db->patches[i].patch_hex, sizeof(db->patches[i].patch_hex), patch_hex);
            db->dirty = true;
            return RE_OK;
        }
    }
    if (db->n_patches >= RE_DBG_DB_MAX_PATCH)
        return RE_E_RANGE;
    db->patches[db->n_patches].rva = rva;
    db->patches[db->n_patches].size = size;
    copy_cstr(db->patches[db->n_patches].orig_hex, sizeof(db->patches[0].orig_hex), orig_hex);
    copy_cstr(db->patches[db->n_patches].patch_hex, sizeof(db->patches[0].patch_hex), patch_hex);
    db->n_patches++;
    db->dirty = true;
    return RE_OK;
}

re_err_code_t re_dbg_db_add_func(re_dbg_db_t *db, uint64_t start_rva, uint64_t end_rva,
                                 const char *name) {
    size_t i;
    if (!db || !name)
        return RE_E_USAGE;
    for (i = 0; i < db->n_funcs; i++) {
        if (db->funcs[i].start_rva == start_rva) {
            db->funcs[i].end_rva = end_rva;
            copy_cstr(db->funcs[i].name, sizeof(db->funcs[i].name), name);
            db->dirty = true;
            return RE_OK;
        }
    }
    if (db->n_funcs >= RE_DBG_DB_MAX_FUNC)
        return RE_E_RANGE;
    db->funcs[db->n_funcs].start_rva = start_rva;
    db->funcs[db->n_funcs].end_rva = end_rva;
    copy_cstr(db->funcs[db->n_funcs].name, sizeof(db->funcs[0].name), name);
    db->n_funcs++;
    db->dirty = true;
    return RE_OK;
}

re_err_code_t re_dbg_db_find_func(const re_dbg_db_t *db, uint64_t rva, re_dbg_db_func_t *out) {
    size_t i;
    if (!db || !out)
        return RE_E_USAGE;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < db->n_funcs; i++) {
        if (rva >= db->funcs[i].start_rva && rva < db->funcs[i].end_rva) {
            *out = db->funcs[i];
            return RE_OK;
        }
    }
    return RE_E_NOTFOUND;
}
