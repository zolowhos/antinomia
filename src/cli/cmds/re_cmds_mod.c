// re_cmds_mod.c - modules, symbol resolve, and stack-walk CLI/MCP commands.
// Module: cli (C11).
// Owns: lm/modules, sym, and bt JSON reports on schema antinomia/1.
// Depends: re_cmds.h, re_dbg, re_json. stdout only via re_jw_flush.
#include "cli/cmds/re_cmds.h"

#include "features/debug/re_dbg.h"
#include "utils/json/re_json.h"
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

int re_cmd_modules(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_module_t mods[128];
    size_t n = 0, i, start, lim;
    re_err_code_t c;
    re_jw_t w;
    bool trunc;
    (void)path;
    (void)argc;
    (void)argv;
    c = re_dbg_session_enum_modules(sess(ctx), mods, 128, &n);
    if (c != RE_OK) {
        emit_err(ctx, "modules", c,
                 c == RE_E_USAGE ? "modules requires an attached target" : "modules failed");
        return re_err_exit_code(c);
    }
    start = ctx->offset;
    if (start > n)
        start = n;
    lim = n - start;
    if (lim > ctx->limit)
        lim = ctx->limit;
    trunc = start + lim < n;
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "modules");
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "count", n);
    re_jw_ku64(&w, "offset", start);
    re_jw_ku64(&w, "limit", lim);
    re_jw_kbool(&w, "truncated", trunc);
    re_jw_key(&w, "modules");
    re_jw_arr(&w);
    for (i = 0; i < lim; i++) {
        re_dbg_module_t *m = &mods[start + i];
        re_jw_obj(&w);
        re_jw_khex(&w, "base", m->base, 16);
        re_jw_ku64(&w, "size", m->size);
        re_jw_kcstr(&w, "path", m->path);
        re_jw_kcstr(&w, "build_id", m->build_id);
        re_jw_obj_end(&w);
    }
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_sym(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *query = re_cmd_positional(argc, argv, 0);
    re_dbg_symbol_t info;
    uint64_t addr = 0;
    re_err_code_t c;
    re_jw_t w;
    (void)path;
    if (!query || !query[0]) {
        emit_err(ctx, "sym", RE_E_USAGE, "sym needs module:symbol or symbol");
        return 2;
    }
    c = re_dbg_session_resolve(sess(ctx), query, &addr, &info);
    if (c != RE_OK) {
        emit_err(ctx, "sym", c, c == RE_E_NOTFOUND ? "symbol not found" : "sym failed");
        return re_err_exit_code(c);
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "sym");
    re_jw_kbool(&w, "ok", true);
    re_jw_khex(&w, "addr", addr, 16);
    re_jw_kcstr(&w, "name", info.name);
    re_jw_kcstr(&w, "module", info.module);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

static bool parse_u64_lit(const char *s, uint64_t *out) {
    re_str_t v;
    uint64_t base = 10, acc = 0;
    size_t i = 0;
    if (!s || !s[0] || !out)
        return false;
    v = re_str(s);
    if (v.n > 2 && v.p[0] == '0' && (v.p[1] == 'x' || v.p[1] == 'X')) {
        base = 16;
        i = 2;
    }
    for (; i < v.n; i++) {
        int d;
        char c = v.p[i];
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F')
            d = 10 + (c - 'A');
        else
            return false;
        if ((uint64_t)d >= base)
            return false;
        acc = acc * base + (uint64_t)d;
    }
    *out = acc;
    return true;
}

static int maps_follow(re_ctx_t *ctx, const char *arg) {
    re_dbg_map_t maps[256];
    size_t n = 0;
    uint64_t addr = 0;
    re_err_code_t c;
    re_jw_t w;
    bool hex = arg && arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X');
    c = re_dbg_session_enum_maps(sess(ctx), maps, 256, &n);
    if (c != RE_OK) {
        emit_err(ctx, "maps", c,
                 c == RE_E_USAGE ? "maps requires an attached target" : "maps failed");
        return re_err_exit_code(c);
    }
    if (!arg || !parse_u64_lit(arg, &addr)) {
        emit_err(ctx, "maps", RE_E_USAGE, "maps follow <index|addr>");
        return 2;
    }
    if (!hex && addr < n)
        addr = maps[(size_t)addr].base;
    c = re_dbg_session_follow_dump(sess(ctx), addr);
    if (c != RE_OK) {
        emit_err(ctx, "maps", c, "maps follow failed");
        return re_err_exit_code(c);
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "maps");
    re_jw_kbool(&w, "ok", true);
    re_jw_khex(&w, "follow", addr, 16);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

static int maps_list(re_ctx_t *ctx) {
    re_dbg_map_t maps[256];
    size_t n = 0, i, start, lim;
    re_err_code_t c;
    re_jw_t w;
    bool trunc;
    c = re_dbg_session_enum_maps(sess(ctx), maps, 256, &n);
    if (c != RE_OK) {
        emit_err(ctx, "maps", c,
                 c == RE_E_USAGE ? "maps requires an attached target" : "maps failed");
        return re_err_exit_code(c);
    }
    start = ctx->offset;
    if (start > n)
        start = n;
    lim = n - start;
    if (lim > ctx->limit)
        lim = ctx->limit;
    trunc = start + lim < n;
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "maps");
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "count", n);
    re_jw_ku64(&w, "offset", start);
    re_jw_ku64(&w, "limit", lim);
    re_jw_kbool(&w, "truncated", trunc);
    re_jw_khex(&w, "follow_dump", sess(ctx)->follow_dump, 16);
    re_jw_key(&w, "maps");
    re_jw_arr(&w);
    for (i = 0; i < lim; i++) {
        re_dbg_map_t *m = &maps[start + i];
        char prot[4];
        re_dbg_map_prot_str(m->protect, prot);
        re_jw_obj(&w);
        re_jw_ku64(&w, "index", start + i);
        re_jw_khex(&w, "base", m->base, 16);
        re_jw_ku64(&w, "size", m->size);
        re_jw_kcstr(&w, "protect", prot);
        re_jw_kcstr(&w, "type", re_dbg_map_type_str(m->type));
        re_jw_kcstr(&w, "path", m->path);
        re_jw_obj_end(&w);
    }
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_maps(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *follow = re_cmd_positional(argc, argv, 0);
    (void)path;
    if (follow && re_str_eq_cstr(re_str(follow), "follow"))
        return maps_follow(ctx, re_cmd_positional(argc, argv, 1));
    return maps_list(ctx);
}

int re_cmd_bt(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_frame_t frames[64];
    size_t n = 0, i, start, lim;
    re_err_code_t c;
    re_jw_t w;
    bool trunc;
    (void)path;
    (void)argc;
    (void)argv;
    c = re_dbg_session_bt(sess(ctx), frames, 64, &n);
    if (c != RE_OK) {
        emit_err(ctx, "bt", c, c == RE_E_USAGE ? "bt requires an attached target" : "bt failed");
        return re_err_exit_code(c);
    }
    start = ctx->offset;
    if (start > n)
        start = n;
    lim = n - start;
    if (lim > ctx->limit)
        lim = ctx->limit;
    trunc = start + lim < n;
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "bt");
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "count", n);
    re_jw_kbool(&w, "truncated", trunc);
    re_jw_key(&w, "frames");
    re_jw_arr(&w);
    for (i = 0; i < lim; i++) {
        re_dbg_frame_t *f = &frames[start + i];
        re_jw_obj(&w);
        re_jw_khex(&w, "ip", f->ip, 16);
        re_jw_khex(&w, "sp", f->sp, 16);
        re_jw_khex(&w, "fp", f->fp, 16);
        re_jw_kcstr(&w, "module", f->module);
        re_jw_kcstr(&w, "symbol", f->symbol);
        re_jw_obj_end(&w);
    }
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}
