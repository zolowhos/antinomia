// re_cmds_expr.c - expression, $var, watch, and conditional BP CLI/MCP commands.
// Module: cli (C11).
// Owns: expr/var/watch/bpc/bplog/bpcmd/bpfr handlers and JSON emission.
// Depends: re_cmds.h, re_dbg, re_expr, re_json.
#include "cli/cmds/re_cmds.h"

#include "features/debug/re_dbg.h"
#include "features/expr/re_expr.h"
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
    re_jw_key(&w, "error");
    re_jw_obj(&w);
    re_jw_kcstr(&w, "code", re_err_str(code));
    re_jw_kcstr(&w, "message", msg);
    re_jw_obj_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
}

static bool parse_addr_expr(re_ctx_t *ctx, const char *s, uint64_t *out) {
    char err[RE_EXPR_ERR_MAX];
    if (!s || !out)
        return false;
    return re_expr_eval_u64(sess(ctx), s, out, err, sizeof(err)) == RE_OK;
}

static const char *rest_args(int argc, char **argv, int from) {
    /* Join remaining positionals with spaces for free-form expressions. */
    static char buf[256];
    size_t n = 0;
    int i;
    buf[0] = 0;
    for (i = from; i < argc; i++) {
        re_str_t a = re_str(argv[i]);
        size_t j;
        if (re_str_starts_cstr(a, "--"))
            break;
        if (n && n + 1u < sizeof(buf))
            buf[n++] = ' ';
        for (j = 0; j < a.n && n + 1u < sizeof(buf); j++)
            buf[n++] = a.p[j];
    }
    buf[n] = 0;
    return buf[0] ? buf : NULL;
}

int re_cmd_expr(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *e = rest_args(argc, argv, 0);
    int64_t v = 0;
    char err[RE_EXPR_ERR_MAX];
    re_err_code_t c;
    re_jw_t w;
    (void)path;
    if (!e) {
        emit_err(ctx, "expr", RE_E_USAGE, "expr needs an expression");
        return 2;
    }
    c = re_expr_eval(sess(ctx), e, &v, err, sizeof(err));
    if (c != RE_OK) {
        emit_err(ctx, "expr", c, err[0] ? err : "expression failed");
        return re_err_exit_code(c);
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "expr");
    re_jw_kbool(&w, "ok", true);
    re_jw_kcstr(&w, "expression", e);
    re_jw_ki64(&w, "value", v);
    re_jw_ku64(&w, "uvalue", (uint64_t)v);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_var(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *name = re_cmd_positional(argc, argv, 0);
    const char *val_s = re_cmd_positional(argc, argv, 1);
    int64_t v = 0;
    re_err_code_t c;
    re_jw_t w;
    (void)path;
    if (!name) {
        emit_err(ctx, "var", RE_E_USAGE, "var needs a name");
        return 2;
    }
    if (name[0] == '$')
        name++;
    if (val_s) {
        char err[RE_EXPR_ERR_MAX];
        if (re_expr_eval(sess(ctx), val_s, &v, err, sizeof(err)) != RE_OK) {
            emit_err(ctx, "var", RE_E_USAGE, err[0] ? err : "var value must be an expression");
            return 2;
        }
        c = re_dbg_session_var_set(sess(ctx), name, v);
        if (c != RE_OK) {
            emit_err(ctx, "var", c, "var set failed");
            return re_err_exit_code(c);
        }
    } else {
        c = re_dbg_session_var_get(sess(ctx), name, &v);
        if (c != RE_OK) {
            emit_err(ctx, "var", c, "var not found");
            return re_err_exit_code(c);
        }
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "var");
    re_jw_kbool(&w, "ok", true);
    re_jw_kcstr(&w, "name", name);
    re_jw_ki64(&w, "value", v);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_watch(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *e = rest_args(argc, argv, 0);
    re_err_code_t c;
    re_jw_t w;
    (void)path;
    if (!e) {
        emit_err(ctx, "watch", RE_E_USAGE, "watch needs an expression");
        return 2;
    }
    c = re_dbg_session_watch_add(sess(ctx), e);
    if (c != RE_OK) {
        emit_err(ctx, "watch", c, "watch add failed");
        return re_err_exit_code(c);
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "watch");
    re_jw_kbool(&w, "ok", true);
    re_jw_kcstr(&w, "expression", e);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_watchc(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *a = re_cmd_positional(argc, argv, 0);
    size_t idx = (size_t)-1;
    re_err_code_t c;
    (void)path;
    if (a && !(a[0] == '*' && a[1] == 0) && !(a[0] == 'a' || a[0] == 'A')) {
        uint64_t n = 0;
        size_t i;
        for (i = 0; a[i]; i++) {
            if (a[i] < '0' || a[i] > '9') {
                emit_err(ctx, "watchc", RE_E_USAGE, "watchc needs index or *");
                return 2;
            }
            n = n * 10u + (uint64_t)(a[i] - '0');
        }
        idx = (size_t)n;
    }
    c = re_dbg_session_watch_clear(sess(ctx), idx);
    if (c != RE_OK) {
        emit_err(ctx, "watchc", c, "watch clear failed");
        return re_err_exit_code(c);
    }
    {
        re_jw_t w;
        re_jw_init(&w, ctx->arena);
        re_jw_obj(&w);
        re_jw_kcstr(&w, "schema", "antinomia/1");
        re_jw_kcstr(&w, "tool", "watchc");
        re_jw_kbool(&w, "ok", true);
        re_jw_obj_end(&w);
        re_jw_flush(&w, re_ctx_out(ctx));
    }
    return 0;
}

int re_cmd_watchlist(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_watch_t list[RE_DBG_MAX_WATCH];
    size_t n = 0, i;
    re_jw_t w;
    (void)path;
    (void)argc;
    (void)argv;
    (void)re_dbg_session_watch_list(sess(ctx), list, RE_DBG_MAX_WATCH, &n);
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "watchlist");
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "count", n);
    re_jw_key(&w, "watches");
    re_jw_arr(&w);
    for (i = 0; i < n; i++) {
        int64_t v = 0;
        char err[RE_EXPR_ERR_MAX];
        re_err_code_t c = re_dbg_session_watch_eval(sess(ctx), i, &v, err, sizeof(err));
        re_jw_obj(&w);
        re_jw_ku64(&w, "index", i);
        re_jw_kcstr(&w, "expression", list[i].expr);
        re_jw_kbool(&w, "ok", c == RE_OK);
        if (c == RE_OK)
            re_jw_ki64(&w, "value", v);
        else
            re_jw_kcstr(&w, "error", err[0] ? err : re_err_str(c));
        re_jw_obj_end(&w);
    }
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

static int bp_field_cmd(re_ctx_t *ctx, const char *tool, const char *field, int argc, char **argv) {
    const char *addr_s = re_cmd_positional(argc, argv, 0);
    const char *val = rest_args(argc, argv, 1);
    uint64_t addr = 0;
    re_err_code_t c;
    re_jw_t w;
    if (!addr_s || !parse_addr_expr(ctx, addr_s, &addr)) {
        emit_err(ctx, tool, RE_E_USAGE, "needs address expression");
        return 2;
    }
    if (!val)
        val = "";
    c = re_dbg_session_bp_set_cond(sess(ctx), addr, field, val);
    if (c != RE_OK) {
        emit_err(ctx, tool, c, "bp field update failed");
        return re_err_exit_code(c);
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", tool);
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "addr", addr);
    re_jw_kcstr(&w, "field", field);
    re_jw_kcstr(&w, "value", val);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_bpc(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    return bp_field_cmd(ctx, "bpc", "break", argc, argv);
}

int re_cmd_bplog(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    return bp_field_cmd(ctx, "bplog", "log", argc, argv);
}

int re_cmd_bplogc(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    return bp_field_cmd(ctx, "bplogc", "logcond", argc, argv);
}

int re_cmd_bpcmd(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    return bp_field_cmd(ctx, "bpcmd", "cmd", argc, argv);
}

int re_cmd_bpcmdc(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    return bp_field_cmd(ctx, "bpcmdc", "cmdcond", argc, argv);
}

int re_cmd_bpfr(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    return bp_field_cmd(ctx, "bpfr", "fast", argc, argv);
}
