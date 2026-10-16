// re_cmds_thr.c - thread list, switch, suspend, and resume CLI/MCP commands.
// Module: cli (C11).
// Owns: threads/thread/suspend/resume JSON reports on schema antinomia/1.
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
    re_jw_kcstr(&w, "code", re_err_json_code(code));
    re_jw_kcstr(&w, "message", msg);
    re_jw_obj_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    if (ctx->err)
        RE_ERR_SET(ctx->err, code, msg);
}

static bool parse_u32(const char *s, uint32_t *out) {
    uint64_t acc = 0;
    size_t i = 0;
    if (!s || !s[0] || !out)
        return false;
    for (; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9')
            return false;
        acc = acc * 10u + (uint64_t)(s[i] - '0');
        if (acc > 0xffffffffull)
            return false;
    }
    *out = (uint32_t)acc;
    return true;
}

int re_cmd_threads(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_thread_t thr[64];
    size_t n = 0, i, start, lim;
    re_err_code_t c;
    re_jw_t w;
    bool trunc;
    (void)path;
    (void)argc;
    (void)argv;
    c = re_dbg_session_enum_threads(sess(ctx), thr, 64, &n);
    if (c != RE_OK) {
        emit_err(ctx, "threads", c,
                 c == RE_E_USAGE ? "threads requires an attached target" : "threads failed");
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
    re_jw_kcstr(&w, "tool", "threads");
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "current", sess(ctx)->tid);
    re_jw_ku64(&w, "count", n);
    re_jw_kbool(&w, "truncated", trunc);
    re_jw_key(&w, "threads");
    re_jw_arr(&w);
    for (i = 0; i < lim; i++) {
        re_dbg_thread_t *t = &thr[start + i];
        re_jw_obj(&w);
        re_jw_ku64(&w, "tid", t->tid);
        re_jw_khex(&w, "rip", t->rip, 16);
        re_jw_kbool(&w, "suspended", t->suspended);
        re_jw_kbool(&w, "current", t->tid == sess(ctx)->tid);
        re_jw_obj_end(&w);
    }
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_thread(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *a = re_cmd_positional(argc, argv, 0);
    uint32_t tid = 0;
    re_err_code_t c;
    re_jw_t w;
    (void)path;
    if (!parse_u32(a, &tid) || tid == 0) {
        emit_err(ctx, "thread", RE_E_USAGE, "thread <tid>");
        return 2;
    }
    c = re_dbg_session_thread_switch(sess(ctx), tid);
    if (c != RE_OK) {
        emit_err(ctx, "thread", c,
                 c == RE_E_NOTFOUND ? "thread not found" : "thread switch failed");
        return re_err_exit_code(c);
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "thread");
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "tid", sess(ctx)->tid);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_suspend(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *a = re_cmd_positional(argc, argv, 0);
    uint32_t tid = 0;
    re_err_code_t c;
    re_jw_t w;
    (void)path;
    if (a && !parse_u32(a, &tid)) {
        emit_err(ctx, "suspend", RE_E_USAGE, "suspend [tid]");
        return 2;
    }
    c = re_dbg_session_thread_suspend(sess(ctx), tid);
    if (c != RE_OK) {
        emit_err(ctx, "suspend", c, "suspend failed");
        return re_err_exit_code(c);
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "suspend");
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "tid", tid ? tid : sess(ctx)->tid);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_resume(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *a = re_cmd_positional(argc, argv, 0);
    uint32_t tid = 0;
    re_err_code_t c;
    re_jw_t w;
    (void)path;
    if (a && !parse_u32(a, &tid)) {
        emit_err(ctx, "resume", RE_E_USAGE, "resume [tid]");
        return 2;
    }
    c = re_dbg_session_thread_resume(sess(ctx), tid);
    if (c != RE_OK) {
        emit_err(ctx, "resume", c, "resume failed");
        return re_err_exit_code(c);
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "resume");
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "tid", tid ? tid : sess(ctx)->tid);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}
