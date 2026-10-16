// re_cmds_os.c - SEH, handles, signal disposition, and DB save CLI/MCP tools.
// Module: cli (C11).
// Owns: seh/handles/signals/dbsave JSON reports on schema antinomia/1.
// Depends: re_cmds.h, re_dbg_os, re_json. stdout only via re_jw_flush.
#include "cli/cmds/re_cmds.h"

#include "features/debug/re_dbg.h"
#include "features/os/re_dbg_os.h"
#include "utils/json/re_json.h"

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

int re_cmd_seh(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_seh_frame_t frames[RE_DBG_SEH_MAX];
    size_t n = 0, i, start, lim;
    re_err_code_t c;
    re_jw_t w;
    bool trunc;
    (void)path;
    (void)argc;
    (void)argv;
    c = re_dbg_session_enum_seh(sess(ctx), frames, RE_DBG_SEH_MAX, &n);
    if (c != RE_OK) {
        emit_err(ctx, "seh", c,
                 c == RE_E_UNSUPPORTED ? "seh requires the Windows backend"
                 : c == RE_E_USAGE     ? "seh requires an attached target"
                                       : "seh failed");
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
    re_jw_kcstr(&w, "tool", "seh");
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "count", n);
    re_jw_kbool(&w, "truncated", trunc);
    re_jw_key(&w, "frames");
    re_jw_arr(&w);
    for (i = 0; i < lim; i++) {
        re_dbg_seh_frame_t *f = &frames[start + i];
        re_jw_obj(&w);
        re_jw_ku64(&w, "tid", f->tid);
        re_jw_khex(&w, "frame", f->frame, 16);
        re_jw_khex(&w, "handler", f->handler, 16);
        re_jw_khex(&w, "next", f->next, 16);
        re_jw_obj_end(&w);
    }
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_handles(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_handle_t rows[RE_DBG_HANDLE_MAX];
    size_t n = 0, i, start, lim;
    re_err_code_t c;
    re_jw_t w;
    bool trunc;
    (void)path;
    (void)argc;
    (void)argv;
    c = re_dbg_session_enum_handles(sess(ctx), rows, RE_DBG_HANDLE_MAX, &n);
    if (c != RE_OK) {
        emit_err(ctx, "handles", c,
                 c == RE_E_UNSUPPORTED ? "handles requires the Windows backend"
                 : c == RE_E_USAGE     ? "handles requires an attached target"
                                       : "handles failed");
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
    re_jw_kcstr(&w, "tool", "handles");
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "count", n);
    re_jw_ku64(&w, "offset", start);
    re_jw_ku64(&w, "limit", lim);
    re_jw_kbool(&w, "truncated", trunc);
    re_jw_key(&w, "handles");
    re_jw_arr(&w);
    for (i = 0; i < lim; i++) {
        re_dbg_handle_t *h = &rows[start + i];
        re_jw_obj(&w);
        re_jw_khex(&w, "handle", h->handle, 4);
        re_jw_ku64(&w, "type_index", h->type_index);
        re_jw_kcstr(&w, "type", h->type_name);
        re_jw_khex(&w, "access", h->granted_access, 8);
        re_jw_kcstr(&w, "name", h->object_name);
        re_jw_obj_end(&w);
    }
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_signals(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_sig_disp_t rows[RE_DBG_SIG_MAX];
    size_t n = 0, i, start, lim;
    re_err_code_t c;
    re_jw_t w;
    bool trunc;
    (void)path;
    (void)argc;
    (void)argv;
    c = re_dbg_session_enum_signals(sess(ctx), rows, RE_DBG_SIG_MAX, &n);
    if (c != RE_OK) {
        emit_err(ctx, "signals", c,
                 c == RE_E_UNSUPPORTED ? "signals requires the Linux backend"
                 : c == RE_E_USAGE     ? "signals requires an attached target"
                                       : "signals failed");
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
    re_jw_kcstr(&w, "tool", "signals");
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "count", n);
    re_jw_ku64(&w, "offset", start);
    re_jw_ku64(&w, "limit", lim);
    re_jw_kbool(&w, "truncated", trunc);
    re_jw_key(&w, "signals");
    re_jw_arr(&w);
    for (i = 0; i < lim; i++) {
        re_dbg_sig_disp_t *r = &rows[start + i];
        re_jw_obj(&w);
        re_jw_ki64(&w, "signo", r->signo);
        re_jw_kcstr(&w, "name", r->name);
        re_jw_kcstr(&w, "disposition", r->disposition);
        re_jw_obj_end(&w);
    }
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_dbsave(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_err_code_t c;
    re_jw_t w;
    re_dbg_session_t *s = sess(ctx);
    (void)path;
    (void)argc;
    (void)argv;
    c = re_dbg_session_db_save(s);
    if (c != RE_OK) {
        emit_err(ctx, "dbsave", c,
                 c == RE_E_USAGE ? "dbsave needs a loaded module DB path" : "dbsave failed");
        return re_err_exit_code(c);
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "dbsave");
    re_jw_kbool(&w, "ok", true);
    re_jw_kcstr(&w, "path", s->db.db_path);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}
