// re_cmds_bp.c - hardware, memory, and module-load breakpoint commands.
// Module: cli (C11).
// Owns: bph/bpm/bpdll handlers and extended bplist fields.
// Depends: re_cmds.h, re_dbg_advbp, re_json. stdout only via re_jw_flush.
#include "cli/cmds/re_cmds.h"

#include "features/bp/re_dbg_advbp.h"
#include "features/debug/re_dbg.h"
#include "features/expr/re_expr.h"
#include "utils/json/re_json.h"
#include "utils/text/re_hex.h"
#include "utils/text/re_str.h"

#include <string.h>

static re_dbg_session_t *sess(re_ctx_t *ctx) {
    if (ctx->session)
        return ctx->session;
    return re_dbg_session_global();
}

static void emit_result(re_ctx_t *ctx, const char *tool, re_err_code_t code, const char *msg) {
    re_dbg_session_t *s = sess(ctx);
    re_jw_t w;
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", tool);
    re_jw_kbool(&w, "implemented", code != RE_E_UNSUPPORTED);
    re_jw_kbool(&w, "ok", code == RE_OK);
    re_jw_kbool(&w, "attached", s->attached);
    if (code != RE_OK) {
        re_jw_key(&w, "error");
        re_jw_obj(&w);
        re_jw_kcstr(&w, "code", re_err_json_code(code));
        re_jw_kcstr(&w, "message", msg);
        re_jw_obj_end(&w);
    }
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    if (ctx->err && code != RE_OK)
        RE_ERR_SET(ctx->err, code, msg);
}

static int finish(re_ctx_t *ctx, const char *tool, re_err_code_t code, const char *fail) {
    const char *msg = fail;
    if (code != RE_E_UNSUPPORTED && code != RE_OK && sess(ctx)->last_err.msg[0])
        msg = sess(ctx)->last_err.msg;
    else if (code == RE_OK)
        msg = "ok";
    emit_result(ctx, tool, code, msg);
    return re_err_exit_code(code);
}

static bool parse_addr(re_ctx_t *ctx, const char *s, uint64_t *out) {
    char err[RE_EXPR_ERR_MAX];
    re_str_t v;
    uint64_t base = 10, acc = 0;
    size_t i = 0;
    if (!s || !s[0] || !out)
        return false;
    if (ctx && re_expr_eval_u64(sess(ctx), s, out, err, sizeof(err)) == RE_OK)
        return true;
    /* Numeric fallback when no session expr context. */
    v = re_str(s);
    if (v.n > 2 && v.p[0] == '0' && (v.p[1] == 'x' || v.p[1] == 'X')) {
        base = 16;
        i = 2;
    }
    for (; i < v.n; i++) {
        int d = re_hex_val(v.p[i]);
        if (d < 0 || (uint64_t)d >= base)
            return false;
        acc = acc * base + (uint64_t)d;
    }
    *out = acc;
    return true;
}

static bool parse_access(const char *s, re_dbg_bp_access_t *out) {
    if (!s || !s[0] || re_str_eq_cstr(re_str(s), "x") || re_str_eq_cstr(re_str(s), "exec")) {
        *out = RE_DBG_BP_AX_X;
        return true;
    }
    if (re_str_eq_cstr(re_str(s), "w") || re_str_eq_cstr(re_str(s), "write")) {
        *out = RE_DBG_BP_AX_W;
        return true;
    }
    if (re_str_eq_cstr(re_str(s), "rw") || re_str_eq_cstr(re_str(s), "r")) {
        *out = RE_DBG_BP_AX_RW;
        return true;
    }
    return false;
}

int re_cmd_bph(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    uint64_t addr = 0;
    uint8_t size = 1;
    re_dbg_bp_access_t access = RE_DBG_BP_AX_X;
    const char *a = re_cmd_positional(argc, argv, 0);
    const char *sz = re_cmd_positional(argc, argv, 1);
    const char *ax = re_cmd_positional(argc, argv, 2);
    uint64_t n = 0;
    (void)path;
    if (!parse_addr(ctx, a, &addr)) {
        emit_result(ctx, "bph", RE_E_USAGE, "bph <addr> [size] [x|w|rw]");
        return 2;
    }
    if (sz && parse_addr(ctx, sz, &n))
        size = (uint8_t)n;
    if (ax && !parse_access(ax, &access)) {
        emit_result(ctx, "bph", RE_E_USAGE, "access must be x, w, or rw");
        return 2;
    }
    return finish(ctx, "bph", re_dbg_session_hw_bp_set(sess(ctx), addr, size, access),
                  "hardware bp set failed");
}

int re_cmd_bph_clear(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    uint64_t addr = 0;
    const char *a = re_cmd_positional(argc, argv, 0);
    (void)path;
    if (!parse_addr(ctx, a, &addr)) {
        emit_result(ctx, "bphc", RE_E_USAGE, "bphc <addr>");
        return 2;
    }
    return finish(ctx, "bphc", re_dbg_session_hw_bp_clear(sess(ctx), addr), "hw bp clear failed");
}

int re_cmd_bpm(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    uint64_t addr = 0, len = 1;
    re_dbg_bp_access_t access = RE_DBG_BP_AX_W;
    const char *a = re_cmd_positional(argc, argv, 0);
    const char *ls = re_cmd_positional(argc, argv, 1);
    const char *ax = re_cmd_positional(argc, argv, 2);
    (void)path;
    if (!parse_addr(ctx, a, &addr)) {
        emit_result(ctx, "bpm", RE_E_USAGE, "bpm <addr> [len] [w|rw]");
        return 2;
    }
    if (ls && !parse_addr(ctx, ls, &len)) {
        emit_result(ctx, "bpm", RE_E_USAGE, "bpm len must be 1, 2, 4, or 8");
        return 2;
    }
    if (ax && !parse_access(ax, &access)) {
        emit_result(ctx, "bpm", RE_E_USAGE, "access must be w or rw");
        return 2;
    }
    if (access == RE_DBG_BP_AX_X)
        access = RE_DBG_BP_AX_W;
    return finish(ctx, "bpm", re_dbg_session_mem_bp_set(sess(ctx), addr, (size_t)len, access),
                  "memory bp set failed");
}

int re_cmd_bpm_clear(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    uint64_t addr = 0;
    const char *a = re_cmd_positional(argc, argv, 0);
    (void)path;
    if (!parse_addr(ctx, a, &addr)) {
        emit_result(ctx, "bpmc", RE_E_USAGE, "bpmc <addr>");
        return 2;
    }
    return finish(ctx, "bpmc", re_dbg_session_mem_bp_clear(sess(ctx), addr), "mem bp clear failed");
}

int re_cmd_bpdll(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *name = re_cmd_positional(argc, argv, 0);
    (void)path;
    if (!name || !name[0]) {
        emit_result(ctx, "bpdll", RE_E_USAGE, "bpdll <name-substring>");
        return 2;
    }
    return finish(ctx, "bpdll", re_dbg_session_load_bp_set(sess(ctx), name), "bpdll failed");
}

int re_cmd_bpdll_clear(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *name = re_cmd_positional(argc, argv, 0);
    (void)path;
    if (!name || !name[0]) {
        emit_result(ctx, "bpdllc", RE_E_USAGE, "bpdllc <name-substring>");
        return 2;
    }
    return finish(ctx, "bpdllc", re_dbg_session_load_bp_clear(sess(ctx), name), "bpdllc failed");
}
