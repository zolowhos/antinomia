// re_cmds_trace.c - trace start/stop/query/save/load command handlers.
// Module: cli (C11).
// Owns: Pack G CLI/MCP verbs emitting antinomia/1 JSON with paging.
// Depends: re_cmds.h, re_dbg_trace, re_json. stdout via re_jw_flush only.
#include "cli/cmds/re_cmds.h"

#include "features/trace/re_dbg_trace.h"
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

static void emit_regs(re_jw_t *w, const re_dbg_regs_t *r) {
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

static int emit_ok_status(re_ctx_t *ctx, const char *tool) {
    re_dbg_session_t *s = sess(ctx);
    re_jw_t w;
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", tool);
    re_jw_kbool(&w, "ok", true);
    re_jw_kbool(&w, "recording", re_dbg_session_trace_recording(s));
    re_jw_ku64(&w, "count", re_dbg_session_trace_count(s));
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

static int do_query(re_ctx_t *ctx, const char *tool) {
    re_dbg_trace_frame_t frames[64];
    size_t n = 0, total = 0, i;
    bool trunc = false;
    re_err_code_t c;
    re_jw_t w;
    size_t limit = ctx->limit ? ctx->limit : 200;
    if (limit > 64)
        limit = 64;
    c = re_dbg_session_trace_query(sess(ctx), ctx->offset, limit, frames, 64, &n, &total, &trunc);
    if (c != RE_OK) {
        emit_err(ctx, tool, c, "trace query failed");
        return re_err_exit_code(c);
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", tool);
    re_jw_kbool(&w, "ok", true);
    re_jw_kbool(&w, "recording", re_dbg_session_trace_recording(sess(ctx)));
    re_jw_ku64(&w, "count", total);
    re_jw_ku64(&w, "offset", ctx->offset);
    re_jw_ku64(&w, "limit", limit);
    re_jw_kbool(&w, "truncated", trunc);
    re_jw_key(&w, "frames");
    re_jw_arr(&w);
    for (i = 0; i < n; i++) {
        char hex[RE_TRACE_BYTES_MAX * 2u + 1u];
        re_jw_obj(&w);
        re_jw_ku64(&w, "index", frames[i].index);
        re_jw_ku64(&w, "tid", frames[i].tid);
        re_jw_khex(&w, "rip", frames[i].regs.rip, 16);
        re_jw_ku64(&w, "size", frames[i].size);
        if (frames[i].size)
            re_hex_encode(hex, frames[i].bytes, frames[i].size);
        else
            hex[0] = 0;
        re_jw_kcstr(&w, "bytes", hex);
        re_jw_kcstr(&w, "text", frames[i].text);
        emit_regs(&w, &frames[i].regs);
        re_jw_obj_end(&w);
    }
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_tracestart(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_err_code_t c;
    (void)path;
    (void)argc;
    (void)argv;
    c = re_dbg_session_trace_start(sess(ctx));
    if (c != RE_OK) {
        emit_err(ctx, "tracestart", c, "tracestart failed");
        return re_err_exit_code(c);
    }
    return emit_ok_status(ctx, "tracestart");
}

int re_cmd_tracestop(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_err_code_t c;
    (void)path;
    (void)argc;
    (void)argv;
    c = re_dbg_session_trace_stop(sess(ctx));
    if (c != RE_OK) {
        emit_err(ctx, "tracestop", c, "tracestop failed");
        return re_err_exit_code(c);
    }
    return emit_ok_status(ctx, "tracestop");
}

int re_cmd_traceclear(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_err_code_t c;
    (void)path;
    (void)argc;
    (void)argv;
    c = re_dbg_session_trace_clear(sess(ctx));
    if (c != RE_OK) {
        emit_err(ctx, "traceclear", c, "traceclear failed");
        return re_err_exit_code(c);
    }
    return emit_ok_status(ctx, "traceclear");
}

int re_cmd_tracestep(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *p = re_cmd_positional(argc, argv, 0);
    uint32_t n = 1;
    re_err_code_t c;
    (void)path;
    if (p && p[0]) {
        uint64_t v = 0;
        size_t i;
        for (i = 0; p[i]; i++) {
            if (p[i] < '0' || p[i] > '9') {
                emit_err(ctx, "tracestep", RE_E_USAGE, "tracestep needs a step count");
                return 2;
            }
            v = v * 10u + (uint64_t)(p[i] - '0');
        }
        if (v == 0 || v > 10000u) {
            emit_err(ctx, "tracestep", RE_E_USAGE, "tracestep count out of range");
            return 2;
        }
        n = (uint32_t)v;
    }
    c = re_dbg_session_trace_step(sess(ctx), n);
    if (c != RE_OK) {
        const char *msg = sess(ctx)->last_err.msg[0] ? sess(ctx)->last_err.msg : "tracestep failed";
        emit_err(ctx, "tracestep", c, msg);
        return re_err_exit_code(c);
    }
    return emit_ok_status(ctx, "tracestep");
}

int re_cmd_tracesave(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *file = re_cmd_positional(argc, argv, 0);
    re_err_code_t c;
    (void)path;
    if (!file || !file[0]) {
        emit_err(ctx, "tracesave", RE_E_USAGE, "tracesave needs a file path");
        return 2;
    }
    c = re_dbg_session_trace_save(sess(ctx), file);
    if (c != RE_OK) {
        emit_err(ctx, "tracesave", c,
                 sess(ctx)->last_err.msg[0] ? sess(ctx)->last_err.msg : "tracesave failed");
        return re_err_exit_code(c);
    }
    return emit_ok_status(ctx, "tracesave");
}

int re_cmd_traceload(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *file = re_cmd_positional(argc, argv, 0);
    re_err_code_t c;
    (void)path;
    if (!file || !file[0]) {
        emit_err(ctx, "traceload", RE_E_USAGE, "traceload needs a file path");
        return 2;
    }
    c = re_dbg_session_trace_load(sess(ctx), file);
    if (c != RE_OK) {
        emit_err(ctx, "traceload", c,
                 sess(ctx)->last_err.msg[0] ? sess(ctx)->last_err.msg : "traceload failed");
        return re_err_exit_code(c);
    }
    return emit_ok_status(ctx, "traceload");
}

int re_cmd_trace(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *sub = re_cmd_positional(argc, argv, 0);
    (void)path;
    if (!sub || !sub[0])
        return do_query(ctx, "trace");
    if (re_str_eq_cstr(re_str(sub), "start") || re_str_eq_cstr(re_str(sub), "on"))
        return re_cmd_tracestart(ctx, path, 0, NULL);
    if (re_str_eq_cstr(re_str(sub), "stop") || re_str_eq_cstr(re_str(sub), "off"))
        return re_cmd_tracestop(ctx, path, 0, NULL);
    if (re_str_eq_cstr(re_str(sub), "clear"))
        return re_cmd_traceclear(ctx, path, 0, NULL);
    if (re_str_eq_cstr(re_str(sub), "step")) {
        /* Shift argv so positional 0 is the count (same as bare tracestep). */
        int off = 0;
        while (off < argc && argv[off] && argv[off][0] == '-' && argv[off][1] == '-')
            off += 2;
        if (off < argc)
            off += 1; /* skip "step" */
        return re_cmd_tracestep(ctx, path, argc - off, argv + off);
    }
    if (re_str_eq_cstr(re_str(sub), "save")) {
        int off = 0;
        while (off < argc && argv[off] && argv[off][0] == '-' && argv[off][1] == '-')
            off += 2;
        if (off < argc)
            off += 1; /* skip "save" */
        if (off >= argc || !argv[off] || !argv[off][0]) {
            emit_err(ctx, "trace", RE_E_USAGE, "trace save needs a path");
            return 2;
        }
        return re_cmd_tracesave(ctx, path, argc - off, argv + off);
    }
    if (re_str_eq_cstr(re_str(sub), "load")) {
        int off = 0;
        while (off < argc && argv[off] && argv[off][0] == '-' && argv[off][1] == '-')
            off += 2;
        if (off < argc)
            off += 1; /* skip "load" */
        if (off >= argc || !argv[off] || !argv[off][0]) {
            emit_err(ctx, "trace", RE_E_USAGE, "trace load needs a path");
            return 2;
        }
        return re_cmd_traceload(ctx, path, argc - off, argv + off);
    }
    if (re_str_eq_cstr(re_str(sub), "query"))
        return do_query(ctx, "trace");
    emit_err(ctx, "trace", RE_E_USAGE, "trace [start|stop|clear|step|save|load|query]");
    return 2;
}

int re_cmd_trace_query(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    return do_query(ctx, "trace.query");
}
