// re_cmds_script.c - command scripts, notes/log, traces, on-BP cmd run.
// Module: cli (C11).
// Owns: script/source/note/notes/log/traces handlers and line dispatch.
// Depends: re_cmds.h, re_script, re_table, re_json.
#include "cli/cmds/re_cmds.h"

#include "features/debug/re_dbg.h"
#include "features/script/re_script.h"
#include "utils/json/re_json.h"
#include "utils/text/re_str.h"
#include "cli/app/re_table.h"

#include <stdio.h>
#include <string.h>

#define SCRIPT_LINE_MAX 512
#define SCRIPT_ARGS_MAX 32

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
    if (ctx->err)
        RE_ERR_SET(ctx->err, code, msg);
}

static const char *join_args(re_arena_t *a, int argc, char **argv, int from) {
    size_t n = 0, i, pos = 0;
    char *buf;
    if (!a || from >= argc)
        return NULL;
    for (i = (size_t)from; i < (size_t)argc; i++) {
        if (i > (size_t)from)
            n++;
        n += re_str(argv[i]).n;
    }
    buf = (char *)re_arena_alloc(a, n + 1u);
    if (!buf)
        return NULL;
    for (i = (size_t)from; i < (size_t)argc; i++) {
        re_str_t w = re_str(argv[i]);
        size_t j;
        if (i > (size_t)from)
            buf[pos++] = ' ';
        for (j = 0; j < w.n; j++)
            buf[pos++] = w.p[j];
    }
    buf[pos] = 0;
    return buf;
}

static bool split_line(const char *line, char **argv, int *argc, re_arena_t *a) {
    *argc = 0;
    while (*line) {
        char quote = 0;
        const char *start;
        size_t n;
        char *word;
        while (*line == ' ' || *line == '\t')
            line++;
        if (!*line || *line == '#')
            break;
        if (*line == '"' || *line == '\'')
            quote = *line++;
        start = line;
        while (*line && (quote ? *line != quote : (*line != ' ' && *line != '\t')))
            line++;
        n = (size_t)(line - start);
        if (*argc >= SCRIPT_ARGS_MAX)
            return false;
        word = (char *)re_arena_alloc(a, n + 1u);
        if (!word)
            return false;
        memcpy(word, start, n);
        word[n] = 0;
        argv[(*argc)++] = word;
        if (quote && *line == quote)
            line++;
    }
    return true;
}

static void copy_name(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    if (!dst || cap == 0)
        return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

/* Run one command line; name_out receives a stable copy into name_buf. */
static int exec_line(re_ctx_t *parent, const char *line, char *name_buf, size_t name_cap,
                     bool *ok_out) {
    re_arena_t a;
    char *argv[SCRIPT_ARGS_MAX];
    int argc = 0;
    int first = 0;
    re_err_t err;
    re_ctx_t local;
    const re_cmd_t *cmd;
    FILE *tmp;
    int code;
    if (name_buf && name_cap)
        name_buf[0] = 0;
    if (ok_out)
        *ok_out = false;
    re_arena_init(&a, 64u * 1024u);
    if (!split_line(line, argv, &argc, &a) || argc < 1) {
        re_arena_free(&a);
        return 0;
    }
    copy_name(name_buf, name_cap, argv[0]);
    if (re_str_eq_cstr(re_str(argv[0]), "script") || re_str_eq_cstr(re_str(argv[0]), "source")) {
        re_arena_free(&a);
        return 2;
    }
    cmd = re_cmd_find(argv[0]);
    if (!cmd || !cmd->fn) {
        re_arena_free(&a);
        return 2;
    }
    copy_name(name_buf, name_cap, cmd->name);
    tmp = tmpfile();
    err.code = RE_OK;
    local = *parent;
    local.arena = &a;
    local.err = &err;
    local.stream = tmp ? (void *)tmp : parent->stream;
    local.session = sess(parent);
    if (!re_cmd_parse(&local, argc - 1, argv + 1, &first, &err)) {
        if (tmp)
            fclose(tmp);
        re_arena_free(&a);
        return 2;
    }
    code = cmd->fn(&local, first < argc - 1 ? argv[1 + first] : NULL, argc - 1 - first,
                   argv + 1 + first);
    if (tmp)
        fclose(tmp);
    if (ok_out)
        *ok_out = (code == 0 && err.code == RE_OK);
    re_arena_free(&a);
    return code;
}

int re_cmd_run_pending(re_ctx_t *ctx) {
    char cmd[RE_DBG_BP_COND_MAX];
    char name[48];
    bool ok = false;
    if (!re_dbg_session_consume_pending_cmd(sess(ctx), cmd, sizeof(cmd)))
        return 0;
    return exec_line(ctx, cmd, name, sizeof(name), &ok);
}

static int emit_page_strings(re_ctx_t *ctx, const char *tool, const char **rows, size_t n) {
    size_t start, lim, i;
    bool trunc;
    re_jw_t w;
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
    re_jw_kcstr(&w, "tool", tool);
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "count", n);
    re_jw_ku64(&w, "offset", start);
    re_jw_ku64(&w, "limit", lim);
    re_jw_kbool(&w, "truncated", trunc);
    re_jw_key(&w, "items");
    re_jw_arr(&w);
    for (i = 0; i < lim; i++)
        re_jw_cstr(&w, rows[start + i]);
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_script(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *file = path && path[0] ? path : re_cmd_positional(argc, argv, 0);
    FILE *fp;
    char line[SCRIPT_LINE_MAX];
    size_t ran = 0, failed = 0;
    re_jw_t w;
    if (!file || !file[0]) {
        emit_err(ctx, "script", RE_E_USAGE, "script needs a file path");
        return 2;
    }
    fp = fopen(file, "rb");
    if (!fp) {
        emit_err(ctx, "script", RE_E_IO, "could not open script file");
        return re_err_exit_code(RE_E_IO);
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "script");
    re_jw_kcstr(&w, "path", file);
    re_jw_key(&w, "lines");
    re_jw_arr(&w);
    while (fgets(line, (int)sizeof(line), fp)) {
        size_t n = re_str(line).n;
        char name[48];
        bool ok = false;
        int code;
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = 0;
        if (n == 0 || line[0] == '#')
            continue;
        code = exec_line(ctx, line, name, sizeof(name), &ok);
        ran++;
        if (!ok)
            failed++;
        re_jw_obj(&w);
        re_jw_kcstr(&w, "cmd", name[0] ? name : line);
        re_jw_kbool(&w, "ok", ok);
        re_jw_ki64(&w, "code", code);
        re_jw_obj_end(&w);
    }
    fclose(fp);
    re_jw_arr_end(&w);
    re_jw_ku64(&w, "ran", ran);
    re_jw_ku64(&w, "failed", failed);
    re_jw_kbool(&w, "ok", failed == 0);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return failed ? 1 : 0;
}

int re_cmd_note(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *text = join_args(ctx->arena, argc, argv, 0);
    re_jw_t w;
    (void)path;
    if (!text || !text[0]) {
        emit_err(ctx, "note", RE_E_USAGE, "note needs text");
        return 2;
    }
    if (re_dbg_session_note_add(sess(ctx), text) != RE_OK) {
        emit_err(ctx, "note", RE_E_RANGE, "note add failed");
        return 1;
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "note");
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "count", sess(ctx)->notes_n);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_notes(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *rows[RE_DBG_NOTE_MAX];
    size_t n = 0;
    (void)path;
    (void)argc;
    (void)argv;
    (void)re_dbg_session_note_list(sess(ctx), rows, RE_DBG_NOTE_MAX, &n);
    return emit_page_strings(ctx, "notes", rows, n);
}

int re_cmd_log(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *text = join_args(ctx->arena, argc, argv, 0);
    const char *rows[RE_DBG_BP_LOG_MAX];
    size_t n = 0;
    re_jw_t w;
    (void)path;
    if (text && text[0]) {
        if (re_dbg_session_log_add(sess(ctx), text) != RE_OK) {
            emit_err(ctx, "log", RE_E_RANGE, "log add failed");
            return 1;
        }
        re_jw_init(&w, ctx->arena);
        re_jw_obj(&w);
        re_jw_kcstr(&w, "schema", "antinomia/1");
        re_jw_kcstr(&w, "tool", "log");
        re_jw_kbool(&w, "ok", true);
        re_jw_ku64(&w, "count", sess(ctx)->bp_log_n);
        re_jw_obj_end(&w);
        re_jw_flush(&w, re_ctx_out(ctx));
        return 0;
    }
    (void)re_dbg_session_log_list(sess(ctx), rows, RE_DBG_BP_LOG_MAX, &n);
    return emit_page_strings(ctx, "log", rows, n);
}

int re_cmd_traces(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_trace_rec_t recs[RE_DBG_TRACE_MAX];
    size_t n = 0, start, lim, i;
    bool trunc;
    re_jw_t w;
    const char *clear = re_cmd_positional(argc, argv, 0);
    (void)path;
    if (clear && re_str_eq_cstr(re_str(clear), "clear")) {
        (void)re_dbg_session_trace_clear(sess(ctx));
        re_jw_init(&w, ctx->arena);
        re_jw_obj(&w);
        re_jw_kcstr(&w, "schema", "antinomia/1");
        re_jw_kcstr(&w, "tool", "traces");
        re_jw_kbool(&w, "ok", true);
        re_jw_kcstr(&w, "action", "clear");
        re_jw_obj_end(&w);
        re_jw_flush(&w, re_ctx_out(ctx));
        return 0;
    }
    (void)re_dbg_session_trace_list(sess(ctx), recs, RE_DBG_TRACE_MAX, &n);
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
    re_jw_kcstr(&w, "tool", "traces");
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "count", n);
    re_jw_ku64(&w, "offset", start);
    re_jw_ku64(&w, "limit", lim);
    re_jw_kbool(&w, "truncated", trunc);
    re_jw_key(&w, "traces");
    re_jw_arr(&w);
    for (i = 0; i < lim; i++) {
        re_dbg_trace_rec_t *r = &recs[start + i];
        re_jw_obj(&w);
        re_jw_khex(&w, "rip", r->rip, 16);
        re_jw_khex(&w, "rax", r->rax, 16);
        re_jw_khex(&w, "rcx", r->rcx, 16);
        re_jw_khex(&w, "rdx", r->rdx, 16);
        re_jw_obj_end(&w);
    }
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}
