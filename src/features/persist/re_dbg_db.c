// re_dbg_db.c - load and save per-module annotation JSON (breakpoints/labels/comments).
// Module: feature/persist (C11).
// Owns: the on-disk schema and the in-memory annotation tables.
// Depends: re_dbg_db.h, re_json, re_jr, re_buf. No malloc; arena scratch only.
#include "features/persist/re_dbg_db.h"

#include "utils/json/re_jr.h"
#include "utils/json/re_json.h"
#include "utils/mem/re_buf.h"
#include "utils/text/re_str.h"

#include <stdio.h>
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

static uint64_t parse_rva(const re_jr_t *v) {
    re_str_t s;
    uint64_t n = 0;
    size_t i = 0;
    if (!v)
        return 0;
    if (v->kind == RE_JR_NUM)
        return (uint64_t)v->num;
    if (v->kind != RE_JR_STR)
        return 0;
    s = v->str;
    if (s.n >= 2 && s.p[0] == '0' && (s.p[1] == 'x' || s.p[1] == 'X'))
        i = 2;
    for (; i < s.n; i++) {
        char c = s.p[i];
        uint64_t d = 0;
        if (c >= '0' && c <= '9')
            d = (uint64_t)(c - '0');
        else if (c >= 'a' && c <= 'f')
            d = (uint64_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            d = (uint64_t)(c - 'A' + 10);
        else
            break;
        n = (n << 4) | d;
    }
    return n;
}

void re_dbg_db_init(re_dbg_db_t *db) {
    if (!db)
        return;
    memset(db, 0, sizeof(*db));
}

void re_dbg_db_clear(re_dbg_db_t *db) {
    if (!db)
        return;
    memset(db, 0, sizeof(*db));
}

re_err_code_t re_dbg_db_path_for(const char *module_path, char *out, size_t cap) {
    size_t n = 0;
    const char *suf = ".antinomia.json";
    size_t sn = 15;
    if (!module_path || !module_path[0] || !out || cap < 2)
        return RE_E_USAGE;
    while (module_path[n])
        n++;
    if (n + sn + 1 > cap)
        return RE_E_RANGE;
    memcpy(out, module_path, n);
    memcpy(out + n, suf, sn + 1);
    return RE_OK;
}

static re_err_code_t load_bps(re_dbg_db_t *db, const re_jr_t *arr) {
    size_t i;
    if (!arr || arr->kind != RE_JR_ARR)
        return RE_OK;
    for (i = 0; i < arr->count && db->n_bps < RE_DBG_DB_MAX_BP; i++) {
        const re_jr_t *o = &arr->items[i];
        const re_jr_t *rv;
        const re_jr_t *en;
        if (!o || o->kind != RE_JR_OBJ)
            continue;
        rv = re_jr_get(o, "rva");
        en = re_jr_get(o, "enabled");
        db->bps[db->n_bps].rva = parse_rva(rv);
        db->bps[db->n_bps].enabled = en ? re_jr_bool(en, true) : true;
        db->n_bps++;
    }
    return RE_OK;
}

static re_err_code_t load_labels(re_dbg_db_t *db, const re_jr_t *arr) {
    size_t i;
    if (!arr || arr->kind != RE_JR_ARR)
        return RE_OK;
    for (i = 0; i < arr->count && db->n_labels < RE_DBG_DB_MAX_LABEL; i++) {
        const re_jr_t *o = &arr->items[i];
        const re_jr_t *rv;
        const re_jr_t *nm;
        if (!o || o->kind != RE_JR_OBJ)
            continue;
        rv = re_jr_get(o, "rva");
        nm = re_jr_get(o, "name");
        db->labels[db->n_labels].rva = parse_rva(rv);
        copy_cstr(db->labels[db->n_labels].name, sizeof(db->labels[0].name),
                  nm && nm->kind == RE_JR_STR ? nm->str.p : "");
        db->n_labels++;
    }
    return RE_OK;
}

static re_err_code_t load_comments(re_dbg_db_t *db, const re_jr_t *arr) {
    size_t i;
    if (!arr || arr->kind != RE_JR_ARR)
        return RE_OK;
    for (i = 0; i < arr->count && db->n_comments < RE_DBG_DB_MAX_COMMENT; i++) {
        const re_jr_t *o = &arr->items[i];
        const re_jr_t *rv;
        const re_jr_t *tx;
        if (!o || o->kind != RE_JR_OBJ)
            continue;
        rv = re_jr_get(o, "rva");
        tx = re_jr_get(o, "text");
        db->comments[db->n_comments].rva = parse_rva(rv);
        copy_cstr(db->comments[db->n_comments].text, sizeof(db->comments[0].text),
                  tx && tx->kind == RE_JR_STR ? tx->str.p : "");
        db->n_comments++;
    }
    return RE_OK;
}

re_err_code_t re_dbg_db_load(re_dbg_db_t *db, re_arena_t *scratch, const char *module_path) {
    re_file_t f;
    re_jr_t root;
    char path[RE_DBG_DB_PATH_MAX];
    re_err_code_t c;
    if (!db || !scratch || !module_path || !module_path[0])
        return RE_E_USAGE;
    re_dbg_db_clear(db);
    c = re_dbg_db_path_for(module_path, path, sizeof(path));
    if (c != RE_OK)
        return c;
    copy_cstr(db->module_path, sizeof(db->module_path), module_path);
    copy_cstr(db->db_path, sizeof(db->db_path), path);
    c = re_file_open(path, scratch, &f);
    if (c != RE_OK) {
        db->loaded = true;
        return RE_OK; /* missing file => empty db */
    }
    if (!re_jr_parse(scratch, (const char *)f.whole.p, f.whole.n, &root) ||
        root.kind != RE_JR_OBJ) {
        re_file_close(&f);
        return RE_E_MALFORMED;
    }
    (void)load_bps(db, re_jr_get(&root, "breakpoints"));
    (void)load_labels(db, re_jr_get(&root, "labels"));
    (void)load_comments(db, re_jr_get(&root, "comments"));
    re_file_close(&f);
    db->loaded = true;
    db->dirty = false;
    return RE_OK;
}

static void emit_bps(re_jw_t *w, const re_dbg_db_t *db) {
    size_t i;
    re_jw_key(w, "breakpoints");
    re_jw_arr(w);
    for (i = 0; i < db->n_bps; i++) {
        re_jw_obj(w);
        re_jw_khex(w, "rva", db->bps[i].rva, 0);
        re_jw_kbool(w, "enabled", db->bps[i].enabled);
        re_jw_obj_end(w);
    }
    re_jw_arr_end(w);
}

static void emit_labels(re_jw_t *w, const re_dbg_db_t *db) {
    size_t i;
    re_jw_key(w, "labels");
    re_jw_arr(w);
    for (i = 0; i < db->n_labels; i++) {
        re_jw_obj(w);
        re_jw_khex(w, "rva", db->labels[i].rva, 0);
        re_jw_kcstr(w, "name", db->labels[i].name);
        re_jw_obj_end(w);
    }
    re_jw_arr_end(w);
}

static void emit_comments(re_jw_t *w, const re_dbg_db_t *db) {
    size_t i;
    re_jw_key(w, "comments");
    re_jw_arr(w);
    for (i = 0; i < db->n_comments; i++) {
        re_jw_obj(w);
        re_jw_khex(w, "rva", db->comments[i].rva, 0);
        re_jw_kcstr(w, "text", db->comments[i].text);
        re_jw_obj_end(w);
    }
    re_jw_arr_end(w);
}

re_err_code_t re_dbg_db_save(const re_dbg_db_t *db, re_arena_t *scratch) {
    re_jw_t w;
    FILE *f;
    if (!db || !scratch)
        return RE_E_USAGE;
    if (!db->db_path[0])
        return RE_E_USAGE;
    f = fopen(db->db_path, "wb");
    if (!f)
        return RE_E_IO;
    re_jw_init(&w, scratch);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "module_db");
    re_jw_kcstr(&w, "module", db->module_path);
    emit_bps(&w, db);
    emit_labels(&w, db);
    emit_comments(&w, db);
    re_jw_obj_end(&w);
    if (!re_jw_ok(&w)) {
        fclose(f);
        return RE_E_INTERNAL;
    }
    re_jw_flush(&w, f);
    fclose(f);
    return RE_OK;
}

re_err_code_t re_dbg_db_add_bp(re_dbg_db_t *db, uint64_t rva, bool enabled) {
    size_t i;
    if (!db)
        return RE_E_USAGE;
    for (i = 0; i < db->n_bps; i++) {
        if (db->bps[i].rva == rva) {
            db->bps[i].enabled = enabled;
            db->dirty = true;
            return RE_OK;
        }
    }
    if (db->n_bps >= RE_DBG_DB_MAX_BP)
        return RE_E_RANGE;
    db->bps[db->n_bps].rva = rva;
    db->bps[db->n_bps].enabled = enabled;
    db->n_bps++;
    db->dirty = true;
    return RE_OK;
}

re_err_code_t re_dbg_db_add_label(re_dbg_db_t *db, uint64_t rva, const char *name) {
    size_t i;
    if (!db || !name)
        return RE_E_USAGE;
    for (i = 0; i < db->n_labels; i++) {
        if (db->labels[i].rva == rva) {
            copy_cstr(db->labels[i].name, sizeof(db->labels[i].name), name);
            db->dirty = true;
            return RE_OK;
        }
    }
    if (db->n_labels >= RE_DBG_DB_MAX_LABEL)
        return RE_E_RANGE;
    db->labels[db->n_labels].rva = rva;
    copy_cstr(db->labels[db->n_labels].name, sizeof(db->labels[0].name), name);
    db->n_labels++;
    db->dirty = true;
    return RE_OK;
}

re_err_code_t re_dbg_db_add_comment(re_dbg_db_t *db, uint64_t rva, const char *text) {
    size_t i;
    if (!db || !text)
        return RE_E_USAGE;
    for (i = 0; i < db->n_comments; i++) {
        if (db->comments[i].rva == rva) {
            copy_cstr(db->comments[i].text, sizeof(db->comments[i].text), text);
            db->dirty = true;
            return RE_OK;
        }
    }
    if (db->n_comments >= RE_DBG_DB_MAX_COMMENT)
        return RE_E_RANGE;
    db->comments[db->n_comments].rva = rva;
    copy_cstr(db->comments[db->n_comments].text, sizeof(db->comments[0].text), text);
    db->n_comments++;
    db->dirty = true;
    return RE_OK;
}
