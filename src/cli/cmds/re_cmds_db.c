// re_cmds_db.c - source line, bookmark, patch list, and function-cache commands.
// Module: cli (C11).
// Owns: src/bm/patches/funcs JSON reports on schema antinomia/1.
// Depends: re_cmds.h, re_dbg, re_sym_prov, re_json. stdout via re_jw_flush.
#include "cli/cmds/re_cmds.h"

#include "features/debug/re_dbg.h"
#include "features/sym/re_sym_prov.h"
#include "utils/json/re_json.h"
#include "utils/text/re_hex.h"
#include "utils/text/re_str.h"

#include <string.h>

static re_dbg_session_t *sess(re_ctx_t *ctx) {
    if (ctx->session)
        return ctx->session;
    return re_dbg_session_global();
}

static void emit_err(re_ctx_t *ctx, const char *tool, re_err_code_t code, const char *msg) {
    re_jw_t w;
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", tool);
    re_jw_kbool(&w, "ok", false);
    re_jw_kbool(&w, "implemented", code != RE_E_UNSUPPORTED);
    re_jw_key(&w, "error");
    re_jw_obj(&w);
    re_jw_kcstr(&w, "code", re_err_str(code));
    re_jw_kcstr(&w, "message", msg);
    re_jw_obj_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    if (ctx->err)
        RE_ERR_SET(ctx->err, code, msg);
}

static bool parse_u64(const char *s, uint64_t *out) {
    re_str_t v;
    uint64_t base = 10;
    size_t i = 0;
    uint64_t acc = 0;
    if (!s || !s[0])
        return false;
    v = re_str(s);
    if (v.n > 2 && v.p[0] == '0' && (v.p[1] == 'x' || v.p[1] == 'X')) {
        base = 16;
        i = 2;
    }
    if (i >= v.n)
        return false;
    for (; i < v.n; i++) {
        int d = re_hex_val(v.p[i]);
        if (d < 0 || (uint64_t)d >= base)
            return false;
        acc = acc * base + (uint64_t)d;
    }
    *out = acc;
    return true;
}

int re_cmd_src(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *a = re_cmd_positional(argc, argv, 0);
    uint64_t addr = 0;
    re_sym_line_t line;
    re_err_code_t c;
    re_jw_t w;
    re_dbg_regs_t regs;
    (void)path;
    if (a) {
        if (!parse_u64(a, &addr)) {
            emit_err(ctx, "src", RE_E_USAGE, "src needs an address");
            return 2;
        }
    } else {
        c = re_dbg_session_get_regs(sess(ctx), 0, &regs);
        if (c != RE_OK) {
            emit_err(ctx, "src", c, "src needs an address or live regs");
            return re_err_exit_code(c);
        }
        addr = regs.rip;
    }
    c = re_dbg_session_source(sess(ctx), addr, &line);
    if (c != RE_OK) {
        emit_err(ctx, "src", c, c == RE_E_NOTFOUND ? "no source line" : "src failed");
        return re_err_exit_code(c);
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "src");
    re_jw_kbool(&w, "ok", true);
    re_jw_khex(&w, "addr", line.addr, 16);
    re_jw_ku64(&w, "line", line.line);
    re_jw_ku64(&w, "column", line.column);
    re_jw_kcstr(&w, "file", line.file);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_bm(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *a = re_cmd_positional(argc, argv, 0);
    const char *name = re_cmd_positional(argc, argv, 1);
    uint64_t rva = 0;
    re_err_code_t c;
    re_jw_t w;
    (void)path;
    if (!a || !parse_u64(a, &rva)) {
        emit_err(ctx, "bm", RE_E_USAGE, "bm needs <rva> [name]");
        return 2;
    }
    c = re_dbg_db_add_bookmark(&sess(ctx)->db, rva, name ? name : "");
    if (c != RE_OK) {
        emit_err(ctx, "bm", c, "bm failed");
        return re_err_exit_code(c);
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "bm");
    re_jw_kbool(&w, "ok", true);
    re_jw_khex(&w, "rva", rva, 0);
    re_jw_kcstr(&w, "name", name ? name : "");
    re_jw_ku64(&w, "count", sess(ctx)->db.n_bookmarks);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_bmlist(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_db_t *db = &sess(ctx)->db;
    size_t i;
    re_jw_t w;
    (void)path;
    (void)argc;
    (void)argv;
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "bmlist");
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "count", db->n_bookmarks);
    re_jw_key(&w, "bookmarks");
    re_jw_arr(&w);
    for (i = 0; i < db->n_bookmarks; i++) {
        re_jw_obj(&w);
        re_jw_khex(&w, "rva", db->bookmarks[i].rva, 0);
        re_jw_kcstr(&w, "name", db->bookmarks[i].name);
        re_jw_obj_end(&w);
    }
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_patches(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *a = re_cmd_positional(argc, argv, 0);
    re_dbg_db_t *db = &sess(ctx)->db;
    re_jw_t w;
    size_t i;
    (void)path;
    if (a) {
        const char *szs = re_cmd_positional(argc, argv, 1);
        const char *orig = re_cmd_positional(argc, argv, 2);
        const char *pat = re_cmd_positional(argc, argv, 3);
        uint64_t rva = 0, sz = 0;
        re_err_code_t c;
        if (!parse_u64(a, &rva) || !szs || !parse_u64(szs, &sz) || !orig || !pat) {
            emit_err(ctx, "patches", RE_E_USAGE, "patches <rva> <size> <orig_hex> <patch_hex>");
            return 2;
        }
        c = re_dbg_db_add_patch(db, rva, (uint32_t)sz, orig, pat);
        if (c != RE_OK) {
            emit_err(ctx, "patches", c, "patches add failed");
            return re_err_exit_code(c);
        }
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "patches");
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "count", db->n_patches);
    re_jw_key(&w, "patches");
    re_jw_arr(&w);
    for (i = 0; i < db->n_patches; i++) {
        re_jw_obj(&w);
        re_jw_khex(&w, "rva", db->patches[i].rva, 0);
        re_jw_ku64(&w, "size", db->patches[i].size);
        re_jw_kcstr(&w, "orig", db->patches[i].orig_hex);
        re_jw_kcstr(&w, "patch", db->patches[i].patch_hex);
        re_jw_obj_end(&w);
    }
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_funcs(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *a = re_cmd_positional(argc, argv, 0);
    re_dbg_db_t *db = &sess(ctx)->db;
    re_jw_t w;
    size_t i;
    (void)path;
    if (a) {
        const char *ends = re_cmd_positional(argc, argv, 1);
        const char *name = re_cmd_positional(argc, argv, 2);
        uint64_t start = 0, end = 0;
        re_err_code_t c;
        if (!parse_u64(a, &start) || !ends || !parse_u64(ends, &end) || !name) {
            emit_err(ctx, "funcs", RE_E_USAGE, "funcs <start_rva> <end_rva> <name>");
            return 2;
        }
        c = re_dbg_db_add_func(db, start, end, name);
        if (c != RE_OK) {
            emit_err(ctx, "funcs", c, "funcs add failed");
            return re_err_exit_code(c);
        }
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "funcs");
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "count", db->n_funcs);
    re_jw_key(&w, "functions");
    re_jw_arr(&w);
    for (i = 0; i < db->n_funcs; i++) {
        re_jw_obj(&w);
        re_jw_khex(&w, "start", db->funcs[i].start_rva, 0);
        re_jw_khex(&w, "end", db->funcs[i].end_rva, 0);
        re_jw_kcstr(&w, "name", db->funcs[i].name);
        re_jw_obj_end(&w);
    }
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}
