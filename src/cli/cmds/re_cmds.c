// re_cmds.c - debug commands wired through the session API to the backend vtable.
// Module: cli (C11).
// Owns: handlers that call re_dbg_session_* and emit antinomia/1 JSON.
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

static void emit_result(re_ctx_t *ctx, const char *tool, re_err_code_t code, const char *msg,
                        bool ok_fields) {
    re_dbg_session_t *s = sess(ctx);
    re_jw_t w;
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", tool);
    re_jw_kbool(&w, "implemented", code != RE_E_UNSUPPORTED);
    re_jw_kbool(&w, "ok", code == RE_OK);
    if (ok_fields) {
        re_jw_kbool(&w, "attached", s->attached);
        re_jw_ku64(&w, "pid", s->pid);
        re_jw_ku64(&w, "tid", s->tid);
    }
    if (code != RE_OK) {
        re_jw_key(&w, "error");
        re_jw_obj(&w);
        re_jw_kcstr(&w, "code", re_err_str(code));
        re_jw_kcstr(&w, "message", msg);
        re_jw_obj_end(&w);
    }
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    if (ctx->err && code != RE_OK)
        RE_ERR_SET(ctx->err, code, msg);
}

static int finish(re_ctx_t *ctx, const char *tool, re_err_code_t code,
                  const char *unsupported_msg) {
    const char *msg = unsupported_msg;
    if (code != RE_E_UNSUPPORTED && code != RE_OK && sess(ctx)->last_err.msg[0])
        msg = sess(ctx)->last_err.msg;
    else if (code == RE_OK)
        msg = "ok";
    emit_result(ctx, tool, code, msg, true);
    return re_err_exit_code(code);
}

static bool parse_pid(const char *pid_s, uint32_t *out, re_err_code_t *code) {
    uint64_t n = 0;
    re_str_t v;
    if (!pid_s || !pid_s[0]) {
        *code = RE_E_USAGE;
        return false;
    }
    v = re_str(pid_s);
    for (size_t i = 0; i < v.n; i++) {
        if (v.p[i] < '0' || v.p[i] > '9') {
            *code = RE_E_USAGE;
            return false;
        }
        n = n * 10u + (uint64_t)(v.p[i] - '0');
    }
    *out = (uint32_t)n;
    *code = RE_OK;
    return true;
}

int re_cmd_status(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_session_t *s = sess(ctx);
    re_jw_t w;
    (void)path;
    (void)argc;
    (void)argv;
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "status");
    re_jw_kbool(&w, "attached", s->attached);
    re_jw_ku64(&w, "pid", s->pid);
    re_jw_ku64(&w, "tid", s->tid);
    re_jw_kcstr(&w, "path", s->path);
    re_jw_kcstr(&w, "backend", re_dbg_backend_name());
    re_jw_key(&w, "last_event");
    re_jw_obj(&w);
    re_jw_kcstr(&w, "kind", re_dbg_event_kind_str(s->last_event.kind));
    re_jw_ku64(&w, "pid", s->last_event.pid);
    re_jw_ku64(&w, "tid", s->last_event.tid);
    re_jw_ku64(&w, "addr", s->last_event.addr);
    re_jw_ki64(&w, "code", s->last_event.code);
    re_jw_obj_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_init(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)argc;
    (void)argv;
    return finish(ctx, "init", re_dbg_session_launch(sess(ctx), path, NULL),
                  "init/launch is not implemented yet; waiting on OS backends");
}

int re_cmd_launch(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)argc;
    (void)argv;
    return finish(ctx, "launch", re_dbg_session_launch(sess(ctx), path, NULL),
                  "launch is not implemented yet; waiting on OS backends");
}

int re_cmd_attach(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    uint32_t pid = 0;
    re_err_code_t pc = RE_OK;
    (void)path;
    if (!parse_pid(re_cmd_positional(argc, argv, 0), &pid, &pc)) {
        emit_result(ctx, "attach", RE_E_USAGE, "attach needs a numeric pid", false);
        return 2;
    }
    return finish(ctx, "attach", re_dbg_session_attach(sess(ctx), pid),
                  "attach is not implemented yet; waiting on OS backends");
}

int re_cmd_detach(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    return finish(ctx, "detach", re_dbg_session_detach(sess(ctx)),
                  "detach is not implemented yet; waiting on OS backends");
}

int re_cmd_kill(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    return finish(ctx, "kill", re_dbg_session_kill(sess(ctx)),
                  "kill is not implemented yet; waiting on OS backends");
}

int re_cmd_wait_event(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_event_t ev;
    re_err_code_t c;
    (void)path;
    (void)argc;
    (void)argv;
    c = re_dbg_session_wait_event(sess(ctx), &ev, 0);
    return finish(ctx, "wait_event", c,
                  "wait_event is not implemented yet; waiting on OS backends");
}

int re_cmd_run(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    return finish(ctx, "run", re_dbg_session_cont(sess(ctx)),
                  "run/g is not available on this backend");
}

int re_cmd_cont(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    return finish(ctx, "cont", re_dbg_session_cont(sess(ctx)),
                  "cont is not available on this backend");
}

int re_cmd_step(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    return finish(ctx, "step", re_dbg_session_step(sess(ctx)),
                  "step is not available on this backend");
}

int re_cmd_step_into(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    return finish(ctx, "sti", re_dbg_session_step(sess(ctx)),
                  "sti/t is not available on this backend");
}

int re_cmd_step_over(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    return finish(ctx, "sto", re_dbg_session_step_over(sess(ctx)),
                  "sto/p is not available on this backend");
}

int re_cmd_pause(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    return finish(ctx, "pause", re_dbg_session_pause(sess(ctx)),
                  "pause is not available on this backend");
}

static bool parse_addr(const char *s, uint64_t *out) {
    re_str_t v;
    uint64_t base = 10;
    size_t i = 0;
    uint64_t acc = 0;
    if (!s || !s[0] || !out)
        return false;
    v = re_str(s);
    if (v.n > 2 && v.p[0] == '0' && (v.p[1] == 'x' || v.p[1] == 'X')) {
        base = 16;
        i = 2;
    }
    if (i >= v.n)
        return false;
    for (; i < v.n; i++) {
        char c = v.p[i];
        uint64_t d;
        if (c >= '0' && c <= '9')
            d = (uint64_t)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f')
            d = (uint64_t)(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F')
            d = (uint64_t)(c - 'A' + 10);
        else
            return false;
        acc = acc * base + d;
    }
    *out = acc;
    return true;
}

int re_cmd_bp(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    uint64_t addr = 0;
    const char *a = re_cmd_positional(argc, argv, 0);
    (void)path;
    if (!a && ctx->off)
        addr = ctx->off;
    else if (!parse_addr(a, &addr)) {
        emit_result(ctx, "bp", RE_E_USAGE, "bp needs an address", false);
        return 2;
    }
    return finish(ctx, "bp", re_dbg_session_bp_set(sess(ctx), addr), "bp set failed");
}

int re_cmd_bc(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    uint64_t addr = 0;
    const char *a = re_cmd_positional(argc, argv, 0);
    (void)path;
    if (!parse_addr(a, &addr)) {
        if (!ctx->off) {
            emit_result(ctx, "bc", RE_E_USAGE, "bc needs an address", false);
            return 2;
        }
        addr = ctx->off;
    }
    return finish(ctx, "bc", re_dbg_session_bp_clear(sess(ctx), addr), "bp clear failed");
}

int re_cmd_be(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    uint64_t addr = 0;
    const char *a = re_cmd_positional(argc, argv, 0);
    (void)path;
    if (!parse_addr(a, &addr)) {
        emit_result(ctx, "be", RE_E_USAGE, "be needs an address", false);
        return 2;
    }
    return finish(ctx, "be", re_dbg_session_bp_enable(sess(ctx), addr), "bp enable failed");
}

int re_cmd_bd(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    uint64_t addr = 0;
    const char *a = re_cmd_positional(argc, argv, 0);
    (void)path;
    if (!parse_addr(a, &addr)) {
        emit_result(ctx, "bd", RE_E_USAGE, "bd needs an address", false);
        return 2;
    }
    return finish(ctx, "bd", re_dbg_session_bp_disable(sess(ctx), addr), "bp disable failed");
}

int re_cmd_bplist(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_bp_info_t list[64];
    size_t n = 0;
    re_err_code_t c;
    re_jw_t w;
    (void)path;
    (void)argc;
    (void)argv;
    c = re_dbg_session_bp_list(sess(ctx), list, 64, &n);
    if (c != RE_OK)
        return finish(ctx, "bplist", c, "bplist failed");
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "bplist");
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "count", n);
    re_jw_key(&w, "breakpoints");
    re_jw_arr(&w);
    for (size_t i = 0; i < n; i++) {
        re_jw_obj(&w);
        re_jw_ku64(&w, "addr", list[i].addr);
        re_jw_kbool(&w, "enabled", list[i].enabled);
        re_jw_kbool(&w, "temporary", list[i].temporary);
        re_jw_obj_end(&w);
    }
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_threads(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    size_t n = 0;
    re_err_code_t c;
    (void)path;
    (void)argc;
    (void)argv;
    c = sess(ctx)->backend->vt->enum_threads(sess(ctx)->backend, NULL, 0, &n);
    return finish(ctx, "threads", c, "threads is not implemented yet; waiting on OS backends");
}

int re_cmd_modules(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    size_t n = 0;
    re_err_code_t c;
    (void)path;
    (void)argc;
    (void)argv;
    c = sess(ctx)->backend->vt->enum_modules(sess(ctx)->backend, NULL, 0, &n);
    return finish(ctx, "modules", c, "modules is not implemented yet; waiting on OS backends");
}
