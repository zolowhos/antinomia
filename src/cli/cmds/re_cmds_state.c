// re_cmds_state.c - agent snapshot and local AI helper commands for MCP/CLI.
// Module: cli (C11).
// Owns: state + explain / suggest_bp / summarize_ex JSON emitters.
// Depends: re_cmds.h, re_ai, re_dbg, re_dbg_advbp, re_json. stdout via re_jw_flush.
#include "cli/cmds/re_cmds.h"

#include "features/ai/re_ai.h"
#include "features/bp/re_dbg_advbp.h"
#include "features/debug/re_dbg.h"
#include "features/script/re_script.h"
#include "utils/json/re_json.h"
#include "utils/text/re_hex.h"

#include <string.h>

#define RE_STATE_DISASM_MAX 32u
#define RE_STATE_BP_MAX 64u
#define RE_STATE_LOG_MAX RE_DBG_BP_LOG_MAX

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

static void emit_regs_obj(re_jw_t *w, const re_dbg_regs_t *r) {
    re_jw_key(w, "regs");
    re_jw_obj(w);
    re_jw_khex(w, "rax", r->rax, 16);
    re_jw_khex(w, "rbx", r->rbx, 16);
    re_jw_khex(w, "rcx", r->rcx, 16);
    re_jw_khex(w, "rdx", r->rdx, 16);
    re_jw_khex(w, "rsi", r->rsi, 16);
    re_jw_khex(w, "rdi", r->rdi, 16);
    re_jw_khex(w, "rbp", r->rbp, 16);
    re_jw_khex(w, "rsp", r->rsp, 16);
    re_jw_khex(w, "r8", r->r8, 16);
    re_jw_khex(w, "r9", r->r9, 16);
    re_jw_khex(w, "r10", r->r10, 16);
    re_jw_khex(w, "r11", r->r11, 16);
    re_jw_khex(w, "r12", r->r12, 16);
    re_jw_khex(w, "r13", r->r13, 16);
    re_jw_khex(w, "r14", r->r14, 16);
    re_jw_khex(w, "r15", r->r15, 16);
    re_jw_khex(w, "rip", r->rip, 16);
    re_jw_khex(w, "rflags", r->rflags, 16);
    re_jw_obj_end(w);
}

static void emit_insn(re_jw_t *w, const re_disasm_insn_t *in) {
    char hex[33];
    re_hex_encode(hex, in->bytes, in->size);
    re_jw_obj(w);
    re_jw_khex(w, "addr", in->addr, 16);
    re_jw_ku64(w, "size", in->size);
    re_jw_kcstr(w, "bytes", hex);
    re_jw_kcstr(w, "text", in->text);
    re_jw_obj_end(w);
}

static void emit_disasm(re_jw_t *w, re_dbg_session_t *s, uint64_t addr, size_t lim) {
    re_disasm_insn_t lines[RE_DISASM_CACHE_N];
    size_t n = 0, i;
    re_err_code_t c;
    if (lim > RE_STATE_DISASM_MAX)
        lim = RE_STATE_DISASM_MAX;
    if (lim > RE_DISASM_CACHE_N)
        lim = RE_DISASM_CACHE_N;
    c = re_dbg_session_disasm(s, addr, lim, lines, &n);
    re_jw_key(w, "disasm");
    re_jw_obj(w);
    re_jw_khex(w, "addr", addr, 16);
    re_jw_ku64(w, "limit", lim);
    re_jw_ku64(w, "count", c == RE_OK ? n : 0);
    re_jw_kbool(w, "ok", c == RE_OK);
    re_jw_key(w, "insns");
    re_jw_arr(w);
    if (c == RE_OK) {
        for (i = 0; i < n; i++)
            emit_insn(w, &lines[i]);
    }
    re_jw_arr_end(w);
    re_jw_obj_end(w);
}

static void emit_bps(re_jw_t *w, re_dbg_session_t *s) {
    re_dbg_bp_ex_t list[RE_STATE_BP_MAX];
    size_t n = 0, i;
    re_err_code_t c = re_dbg_session_bp_list_ex(s, list, RE_STATE_BP_MAX, &n);
    re_jw_key(w, "breakpoints");
    re_jw_arr(w);
    if (c == RE_OK) {
        for (i = 0; i < n; i++) {
            re_jw_obj(w);
            re_jw_kcstr(w, "kind", re_dbg_bp_kind_str(list[i].kind));
            re_jw_khex(w, "addr", list[i].addr, 16);
            re_jw_kbool(w, "enabled", list[i].enabled);
            if (list[i].kind == RE_DBG_BP_KIND_LOAD)
                re_jw_kcstr(w, "name", list[i].name);
            re_jw_obj_end(w);
        }
    }
    re_jw_arr_end(w);
    re_jw_ku64(w, "breakpoint_count", c == RE_OK ? n : 0);
}

static void emit_log(re_jw_t *w, re_dbg_session_t *s, size_t lim) {
    const char *rows[RE_STATE_LOG_MAX];
    size_t n = 0, start, i;
    (void)re_dbg_session_log_list(s, rows, RE_STATE_LOG_MAX, &n);
    if (lim == 0 || lim > RE_STATE_LOG_MAX)
        lim = RE_STATE_LOG_MAX;
    start = n > lim ? n - lim : 0;
    re_jw_key(w, "log");
    re_jw_obj(w);
    re_jw_ku64(w, "total", n);
    re_jw_ku64(w, "limit", lim);
    re_jw_ku64(w, "count", n - start);
    re_jw_key(w, "lines");
    re_jw_arr(w);
    for (i = start; i < n; i++)
        re_jw_cstr(w, rows[i] ? rows[i] : "");
    re_jw_arr_end(w);
    re_jw_obj_end(w);
}

static void emit_empty_disasm(re_jw_t *w) {
    re_jw_key(w, "disasm");
    re_jw_obj(w);
    re_jw_ku64(w, "count", 0);
    re_jw_key(w, "insns");
    re_jw_arr(w);
    re_jw_arr_end(w);
    re_jw_obj_end(w);
}

int re_cmd_state(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_session_t *s = sess(ctx);
    re_dbg_regs_t regs;
    re_jw_t w;
    size_t dlim = ctx->limit ? ctx->limit : 16;
    size_t llim = ctx->limit ? ctx->limit : RE_STATE_LOG_MAX;
    re_err_code_t rc = RE_OK;
    (void)path;
    (void)argc;
    (void)argv;
    memset(&regs, 0, sizeof(regs));
    if (s->attached) {
        rc = re_dbg_session_get_regs(s, 0, &regs);
        if (rc != RE_OK) {
            emit_err(ctx, "state", rc,
                     s->last_err.msg[0] ? s->last_err.msg : "state regs failed");
            return re_err_exit_code(rc);
        }
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "state");
    re_jw_kbool(&w, "ok", true);
    re_jw_kbool(&w, "attached", s->attached);
    re_jw_ku64(&w, "pid", s->pid);
    re_jw_ku64(&w, "tid", s->tid);
    re_jw_kcstr(&w, "path", s->path);
    re_jw_key(&w, "last_event");
    re_jw_obj(&w);
    re_jw_kcstr(&w, "kind", re_dbg_event_kind_str(s->last_event.kind));
    re_jw_khex(&w, "addr", s->last_event.addr, 16);
    re_jw_obj_end(&w);
    emit_regs_obj(&w, &regs);
    if (s->attached)
        emit_disasm(&w, s, regs.rip, dlim);
    else
        emit_empty_disasm(&w);
    emit_bps(&w, s);
    emit_log(&w, s, llim);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
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
            emit_err(ctx, "summarize_ex", RE_E_USAGE, "summarize_ex needs an exception/signal code");
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
