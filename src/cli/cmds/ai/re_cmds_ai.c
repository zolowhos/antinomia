// re_cmds_ai.c - local AI helper commands for MCP/CLI (explain/suggest/exsum).
// Module: cli (C11).
// Owns: explain / suggest_bp / summarize_ex JSON emitters.
// Depends: re_cmds.h, re_ai, re_dbg, re_json. stdout via re_jw_flush.
#include "cli/cmds/re_cmds.h"

#include "features/ai/re_ai.h"
#include "features/debug/re_dbg.h"
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
    re_jw_kcstr(&w, "code", re_err_json_code(code));
    re_jw_kcstr(&w, "message", msg);
    re_jw_obj_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    if (ctx->err)
        RE_ERR_SET(ctx->err, code, msg);
}

static bool parse_u64_local(const char *s, uint64_t *out) {
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

static bool parse_i32_local(const char *s, int32_t *out) {
    uint64_t u = 0;
    if (!s || !s[0] || !out)
        return false;
    if (s[0] == '-') {
        if (!parse_u64_local(s + 1, &u) || u > 0x7fffffffull)
            return false;
        *out = -(int32_t)u;
        return true;
    }
    if (!parse_u64_local(s, &u) || u > 0xffffffffull)
        return false;
    *out = (int32_t)u;
    return true;
}

int re_cmd_explain(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_session_t *s = sess(ctx);
    re_dbg_regs_t regs;
    re_disasm_insn_t lines[4];
    re_ai_explain_t ex;
    re_jw_t w;
    uint64_t addr = 0;
    size_t n = 0;
    re_err_code_t c;
    const char *a = re_cmd_positional(argc, argv, 0);
    (void)path;
    memset(&regs, 0, sizeof(regs));
    if (!s->attached) {
        emit_err(ctx, "explain", RE_E_USAGE, "explain requires an attached target");
        return 2;
    }
    if (a) {
        if (!parse_u64_local(a, &addr)) {
            emit_err(ctx, "explain", RE_E_USAGE, "explain needs a hex/decimal address");
            return 2;
        }
    } else {
        c = re_dbg_session_get_regs(s, 0, &regs);
        if (c != RE_OK) {
            emit_err(ctx, "explain", c, "explain could not read RIP");
            return re_err_exit_code(c);
        }
        addr = regs.rip;
    }
    c = re_dbg_session_disasm(s, addr, 1, lines, &n);
    if (c != RE_OK || n == 0) {
        emit_err(ctx, "explain", c != RE_OK ? c : RE_E_IO, "explain disasm failed");
        return re_err_exit_code(c != RE_OK ? c : RE_E_IO);
    }
    c = re_ai_explain_insn(&lines[0], &ex);
    if (c != RE_OK) {
        emit_err(ctx, "explain", c, "explain failed");
        return re_err_exit_code(c);
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "explain");
    re_jw_kbool(&w, "ok", true);
    re_jw_khex(&w, "addr", lines[0].addr, 16);
    re_jw_kcstr(&w, "text", lines[0].text);
    re_jw_kcstr(&w, "kind", ex.kind);
    re_jw_kcstr(&w, "summary", ex.summary);
    re_jw_kcstr(&w, "detail", ex.detail);
    if (lines[0].comment[0])
        re_jw_kcstr(&w, "comment", lines[0].comment);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_suggest_bp(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_session_t *s = sess(ctx);
    re_ai_bp_site_t sites[RE_AI_BP_MAX];
    size_t n = 0, i, lim;
    re_err_code_t c;
    re_jw_t w;
    (void)path;
    (void)argc;
    (void)argv;
    if (!s->attached) {
        emit_err(ctx, "suggest_bp", RE_E_USAGE, "suggest_bp requires an attached target");
        return 2;
    }
    c = re_ai_suggest_bp(s, sites, RE_AI_BP_MAX, &n);
    if (c != RE_OK) {
        emit_err(ctx, "suggest_bp", c, "suggest_bp failed");
        return re_err_exit_code(c);
    }
    lim = n;
    if (ctx->limit && ctx->limit < lim)
        lim = ctx->limit;
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "suggest_bp");
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "count", lim);
    re_jw_key(&w, "suggestions");
    re_jw_arr(&w);
    for (i = 0; i < lim; i++) {
        re_jw_obj(&w);
        re_jw_kcstr(&w, "query", sites[i].query);
        re_jw_kcstr(&w, "reason", sites[i].reason);
        re_jw_khex(&w, "addr", sites[i].addr, 16);
        re_jw_kbool(&w, "resolved", sites[i].resolved);
        re_jw_obj_end(&w);
    }
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_summarize_ex(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_session_t *s = sess(ctx);
    re_ai_exsum_t sum;
    re_jw_t w;
    const char *a = re_cmd_positional(argc, argv, 0);
    int32_t code = 0;
    bool have = false;
    re_err_code_t c;
    (void)path;
    if (a) {
        if (!parse_i32_local(a, &code)) {
            emit_err(ctx, "summarize_ex", RE_E_USAGE,
                     "summarize_ex needs an exception/signal code");
            return 2;
        }
        have = true;
    }
    c = re_ai_summarize_exception(&s->last_event, code, have, &sum);
    if (c != RE_OK) {
        emit_err(ctx, "summarize_ex", c,
                 "summarize_ex needs last EXCEPTION event or a code argument");
        return re_err_exit_code(c);
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "summarize_ex");
    re_jw_kbool(&w, "ok", true);
    re_jw_ki64(&w, "code", sum.code);
    re_jw_kcstr(&w, "name", sum.name);
    re_jw_kcstr(&w, "summary", sum.summary);
    re_jw_kcstr(&w, "advice", sum.advice);
    re_jw_kbool(&w, "from_event", sum.from_event);
    re_jw_kbool(&w, "first_chance", sum.first_chance);
    re_jw_khex(&w, "addr", sum.addr, 16);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}
