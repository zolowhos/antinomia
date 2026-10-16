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
                  "run/g is not implemented yet; waiting on OS backends");
}

int re_cmd_cont(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    return finish(ctx, "cont", re_dbg_session_cont(sess(ctx)),
                  "cont is not implemented yet; waiting on OS backends");
}

int re_cmd_step(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_err_code_t c;
    (void)path;
    (void)argc;
    (void)argv;
    c = sess(ctx)->backend->vt->step(sess(ctx)->backend);
    return finish(ctx, "step", c, "step is not implemented yet; waiting on OS backends");
}

int re_cmd_regs(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_regs_t regs;
    re_err_code_t c;
    (void)path;
    (void)argc;
    (void)argv;
    c = sess(ctx)->backend->vt->get_regs(sess(ctx)->backend, sess(ctx)->tid, &regs);
    return finish(ctx, "regs", c, "regs is not implemented yet; waiting on OS backends");
}

int re_cmd_set_regs(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_regs_t regs;
    re_err_code_t c;
    (void)path;
    (void)argc;
    (void)argv;
    memset(&regs, 0, sizeof(regs));
    c = sess(ctx)->backend->vt->set_regs(sess(ctx)->backend, sess(ctx)->tid, &regs);
    return finish(ctx, "set_regs", c, "set_regs is not implemented yet; waiting on OS backends");
}

int re_cmd_mem_read(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    uint8_t buf[16];
    re_err_code_t c;
    (void)path;
    (void)argc;
    (void)argv;
    c = sess(ctx)->backend->vt->read_mem(sess(ctx)->backend, ctx->off, buf, sizeof(buf));
    return finish(ctx, "mem_read", c, "mem_read is not implemented yet; waiting on OS backends");
}

int re_cmd_mem_write(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    uint8_t z = 0;
    re_err_code_t c;
    (void)path;
    (void)argc;
    (void)argv;
    c = sess(ctx)->backend->vt->write_mem(sess(ctx)->backend, ctx->off, &z, 1);
    return finish(ctx, "mem_write", c, "mem_write is not implemented yet; waiting on OS backends");
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
