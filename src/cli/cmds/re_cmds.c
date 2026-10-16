// re_cmds.c - Phase 0 debugger command stubs that emit clear not-implemented JSON.
// Module: cli (C11).
// Owns: stub handlers that call the backend vtable and report RE_E_UNSUPPORTED.
// Depends: re_cmds.h, re_dbg, re_json. stdout only via re_jw_flush.
#include "cli/cmds/re_cmds.h"

#include "features/debug/re_dbg.h"
#include "utils/json/re_json.h"
#include "utils/text/re_hex.h"
#include "utils/text/re_str.h"

#include <stdio.h>
#include <string.h>

static void emit_stub(re_ctx_t *ctx, const char *tool, re_err_code_t code, const char *msg) {
    re_jw_t w;
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", tool);
    re_jw_kbool(&w, "implemented", false);
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

static re_dbg_session_t *sess(re_ctx_t *ctx) {
    if (ctx->session)
        return ctx->session;
    return re_dbg_session_global();
}

int re_cmd_status(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    re_dbg_session_t *s = sess(ctx);
    re_jw_t w;
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "status");
    re_jw_kbool(&w, "attached", s->attached);
    re_jw_ku64(&w, "pid", s->pid);
    re_jw_kcstr(&w, "path", s->path);
    re_jw_kcstr(&w, "backend",
#if defined(_WIN32)
                "windows-stub"
#else
                "linux-stub"
#endif
    );
    re_jw_kcstr(&w, "note", "Phase 0: backends link but do not debug yet");
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_launch(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)argc;
    (void)argv;
    re_dbg_session_t *s = sess(ctx);
    re_err_code_t c = s->backend->vt->launch(s->backend, path, NULL);
    emit_stub(ctx, "launch", c, "launch is not implemented in Phase 0");
    return re_err_exit_code(c);
}

int re_cmd_attach(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    const char *pid_s = re_cmd_positional(argc, argv, 0);
    uint32_t pid = 0;
    if (pid_s) {
        uint64_t n = 0;
        re_str_t v = re_str(pid_s);
        for (size_t i = 0; i < v.n; i++) {
            if (v.p[i] < '0' || v.p[i] > '9') {
                emit_stub(ctx, "attach", RE_E_USAGE, "attach needs a numeric pid");
                return 2;
            }
            n = n * 10u + (uint64_t)(v.p[i] - '0');
        }
        pid = (uint32_t)n;
    }
    re_dbg_session_t *s = sess(ctx);
    re_err_code_t c = s->backend->vt->attach(s->backend, pid);
    emit_stub(ctx, "attach", c, "attach is not implemented in Phase 0");
    return re_err_exit_code(c);
}

int re_cmd_detach(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    re_dbg_session_t *s = sess(ctx);
    re_err_code_t c = s->backend->vt->detach(s->backend);
    emit_stub(ctx, "detach", c, "detach is not implemented in Phase 0");
    return re_err_exit_code(c);
}

int re_cmd_wait_event(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    re_dbg_event_t ev;
    re_dbg_session_t *s = sess(ctx);
    re_err_code_t c = s->backend->vt->wait_event(s->backend, &ev, 0);
    emit_stub(ctx, "wait_event", c, "wait_event is not implemented in Phase 0");
    return re_err_exit_code(c);
}

int re_cmd_cont(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    re_dbg_session_t *s = sess(ctx);
    re_err_code_t c = s->backend->vt->cont(s->backend);
    emit_stub(ctx, "cont", c, "cont is not implemented in Phase 0");
    return re_err_exit_code(c);
}

int re_cmd_step(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    re_dbg_session_t *s = sess(ctx);
    re_err_code_t c = s->backend->vt->step(s->backend);
    emit_stub(ctx, "step", c, "step is not implemented in Phase 0");
    return re_err_exit_code(c);
}

int re_cmd_regs(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    re_dbg_regs_t regs;
    re_dbg_session_t *s = sess(ctx);
    re_err_code_t c = s->backend->vt->get_regs(s->backend, 0, &regs);
    emit_stub(ctx, "regs", c, "regs is not implemented in Phase 0");
    return re_err_exit_code(c);
}

int re_cmd_set_regs(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    re_dbg_regs_t regs;
    memset(&regs, 0, sizeof(regs));
    re_dbg_session_t *s = sess(ctx);
    re_err_code_t c = s->backend->vt->set_regs(s->backend, 0, &regs);
    emit_stub(ctx, "set_regs", c, "set_regs is not implemented in Phase 0");
    return re_err_exit_code(c);
}

int re_cmd_mem_read(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    uint8_t buf[16];
    re_dbg_session_t *s = sess(ctx);
    re_err_code_t c = s->backend->vt->read_mem(s->backend, ctx->off, buf, sizeof(buf));
    emit_stub(ctx, "mem_read", c, "mem_read is not implemented in Phase 0");
    return re_err_exit_code(c);
}

int re_cmd_mem_write(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    uint8_t z = 0;
    re_dbg_session_t *s = sess(ctx);
    re_err_code_t c = s->backend->vt->write_mem(s->backend, ctx->off, &z, 1);
    emit_stub(ctx, "mem_write", c, "mem_write is not implemented in Phase 0");
    return re_err_exit_code(c);
}

int re_cmd_threads(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    size_t n = 0;
    re_dbg_session_t *s = sess(ctx);
    re_err_code_t c = s->backend->vt->enum_threads(s->backend, NULL, 0, &n);
    emit_stub(ctx, "threads", c, "threads is not implemented in Phase 0");
    return re_err_exit_code(c);
}

int re_cmd_modules(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    size_t n = 0;
    re_dbg_session_t *s = sess(ctx);
    re_err_code_t c = s->backend->vt->enum_modules(s->backend, NULL, 0, &n);
    emit_stub(ctx, "modules", c, "modules is not implemented in Phase 0");
    return re_err_exit_code(c);
}
