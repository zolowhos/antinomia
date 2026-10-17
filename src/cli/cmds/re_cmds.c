// re_cmds.c - debug commands wired through the session API to the backend vtable.
// Module: cli (C11).
// Owns: handlers that call re_dbg_session_* and emit antinomia/1 JSON.
// Depends: re_cmds.h, re_dbg, re_json. stdout only via re_jw_flush.
#include "cli/cmds/re_cmds.h"

#include "features/bp/re_dbg_advbp.h"
#include "features/debug/re_dbg.h"
#include "features/expr/re_expr.h"
#include "utils/json/re_json.h"
#include "utils/text/re_str.h"

#include <stdlib.h>
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
        re_jw_kcstr(&w, "code", re_err_json_code(code));
        re_jw_kcstr(&w, "message", msg);
        if (code == RE_E_PRIVILEGE) {
            re_err_privilege_t priv;
            re_err_privilege_fill(tool, &priv);
            re_jw_kcstr(&w, "kind", priv.kind);
            re_jw_kcstr(&w, "platform", priv.platform);
            re_jw_kcstr(&w, "hint", priv.hint);
        }
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
    re_jw_kbool(&w, "first_chance", s->last_event.first_chance);
    re_jw_obj_end(&w);
    re_jw_ku64(&w, "caps", re_dbg_session_caps(s).bits);
    re_jw_ku64(&w, "events_coalesced", s->events_coalesced);
    re_jw_ku64(&w, "bp_storms", s->bp_storms);
    re_jw_ki64(&w, "exit_code", s->exit_code);
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
    uint32_t timeout_ms = 0;
    const char *t = re_cmd_positional(argc, argv, 0);
    (void)path;
    if (t && t[0]) {
        unsigned long v = 0;
        char *end = NULL;
        v = strtoul(t, &end, 0);
        if (end && end != t && *end == 0 && v <= 0xfffffffful)
            timeout_ms = (uint32_t)v;
    }
    c = re_dbg_session_wait_event(sess(ctx), &ev, timeout_ms);
    if (c == RE_OK)
        (void)re_cmd_run_pending(ctx);
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

static bool parse_addr(re_ctx_t *ctx, const char *s, uint64_t *out) {
    char err[RE_EXPR_ERR_MAX];
    if (!s || !s[0] || !out)
        return false;
    return re_expr_eval_u64(sess(ctx), s, out, err, sizeof(err)) == RE_OK;
}

int re_cmd_bp(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    uint64_t addr = 0;
    const char *a = re_cmd_positional(argc, argv, 0);
    (void)path;
    if (!a && ctx->off)
        addr = ctx->off;
    else if (!parse_addr(ctx, a, &addr)) {
        emit_result(ctx, "bp", RE_E_USAGE, "bp needs an address expression", false);
        return 2;
    }
    return finish(ctx, "bp", re_dbg_session_bp_set(sess(ctx), addr), "bp set failed");
}

int re_cmd_bc(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    uint64_t addr = 0;
    const char *a = re_cmd_positional(argc, argv, 0);
    (void)path;
    if (!parse_addr(ctx, a, &addr)) {
        if (!ctx->off) {
            emit_result(ctx, "bc", RE_E_USAGE, "bc needs an address expression", false);
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
    if (!parse_addr(ctx, a, &addr)) {
        emit_result(ctx, "be", RE_E_USAGE, "be needs an address expression", false);
        return 2;
    }
    return finish(ctx, "be", re_dbg_session_bp_enable(sess(ctx), addr), "bp enable failed");
}

int re_cmd_bd(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    uint64_t addr = 0;
    const char *a = re_cmd_positional(argc, argv, 0);
    (void)path;
    if (!parse_addr(ctx, a, &addr)) {
        emit_result(ctx, "bd", RE_E_USAGE, "bd needs an address expression", false);
        return 2;
    }
    return finish(ctx, "bd", re_dbg_session_bp_disable(sess(ctx), addr), "bp disable failed");
}

int re_cmd_bplist(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_bp_ex_t list[96];
    size_t n = 0;
    re_err_code_t c;
    re_jw_t w;
    (void)path;
    (void)argc;
    (void)argv;
    c = re_dbg_session_bp_list_ex(sess(ctx), list, 96, &n);
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
        re_jw_kcstr(&w, "kind", re_dbg_bp_kind_str(list[i].kind));
        re_jw_ku64(&w, "addr", list[i].addr);
        re_jw_kbool(&w, "enabled", list[i].enabled);
        re_jw_kbool(&w, "temporary", list[i].temporary);
        if (list[i].kind == RE_DBG_BP_KIND_HW || list[i].kind == RE_DBG_BP_KIND_MEM) {
            re_jw_kcstr(&w, "access", re_dbg_bp_access_str((re_dbg_bp_access_t)list[i].access));
            re_jw_ku64(&w, "size", list[i].size ? list[i].size : list[i].len);
        }
        if (list[i].kind == RE_DBG_BP_KIND_LOAD)
            re_jw_kcstr(&w, "name", list[i].name);
        if (list[i].kind == RE_DBG_BP_KIND_SOFT) {
            re_jw_kbool(&w, "fast_resume", list[i].fast_resume);
            re_jw_ku64(&w, "hit_count", list[i].hit_count);
            re_jw_kcstr(&w, "break_cond", list[i].break_cond);
            re_jw_kcstr(&w, "log_text", list[i].log_text);
            re_jw_kcstr(&w, "log_cond", list[i].log_cond);
            re_jw_kcstr(&w, "cmd", list[i].cmd);
            re_jw_kcstr(&w, "cmd_cond", list[i].cmd_cond);
        }
        re_jw_obj_end(&w);
    }
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

static bool parse_u64_literal(const char *s, uint64_t *out) {
    re_str_t v;
    uint64_t base = 10, acc = 0;
    size_t i = 0;
    if (!s || !s[0] || !out)
        return false;
    v = re_str(s);
    if (v.n > 2 && v.p[0] == '0' && (v.p[1] == 'x' || v.p[1] == 'X')) {
        base = 16;
        i = 2;
    }
    for (; i < v.n; i++) {
        int d;
        char c = v.p[i];
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F')
            d = 10 + (c - 'A');
        else
            return false;
        if ((uint64_t)d >= base)
            return false;
        acc = acc * base + (uint64_t)d;
    }
    *out = acc;
    return true;
}

static bool parse_i32(const char *s, int32_t *out) {
    uint64_t u = 0;
    bool neg = false;
    if (!s || !out)
        return false;
    if (s[0] == '-') {
        neg = true;
        s++;
    }
    if (!parse_u64_literal(s, &u))
        return false;
    *out = neg ? -(int32_t)u : (int32_t)u;
    return true;
}

static bool parse_range(int argc, char **argv, int32_t *lo, int32_t *hi) {
    if (!parse_i32(re_cmd_positional(argc, argv, 0), lo))
        return false;
    if (!re_cmd_positional(argc, argv, 1)) {
        *hi = *lo;
        return true;
    }
    return parse_i32(re_cmd_positional(argc, argv, 1), hi);
}

int re_cmd_exbp(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    int32_t code = 0;
    re_dbg_ex_policy_t pol = RE_DBG_EX_BREAK_PASS;
    int i;
    (void)path;
    if (!parse_i32(re_cmd_positional(argc, argv, 0), &code)) {
        emit_result(ctx, "exbp", RE_E_USAGE, "exbp needs an exception/signal code", false);
        return 2;
    }
    for (i = 0; i < argc; i++) {
        if (re_str_eq_cstr(re_str(argv[i]), "--handle"))
            pol = RE_DBG_EX_BREAK_HANDLE;
    }
    return finish(ctx, "exbp", re_dbg_session_ex_set(sess(ctx), code, code, pol, true),
                  "exbp failed");
}

int re_cmd_exignore(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    int32_t lo = 0, hi = 0;
    (void)path;
    if (!parse_range(argc, argv, &lo, &hi)) {
        emit_result(ctx, "exignore", RE_E_USAGE, "exignore needs lo [hi]", false);
        return 2;
    }
    return finish(ctx, "exignore", re_dbg_session_ex_set(sess(ctx), lo, hi, RE_DBG_EX_IGNORE, true),
                  "exignore failed");
}

int re_cmd_expass(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    int32_t lo = 0, hi = 0;
    (void)path;
    if (!parse_range(argc, argv, &lo, &hi)) {
        emit_result(ctx, "expass", RE_E_USAGE, "expass needs lo [hi]", false);
        return 2;
    }
    return finish(ctx, "expass", re_dbg_session_ex_set(sess(ctx), lo, hi, RE_DBG_EX_PASS, true),
                  "expass failed");
}

int re_cmd_exclear(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    int32_t lo = 0, hi = 0;
    (void)path;
    if (!parse_range(argc, argv, &lo, &hi)) {
        emit_result(ctx, "exclear", RE_E_USAGE, "exclear needs lo [hi]", false);
        return 2;
    }
    return finish(ctx, "exclear", re_dbg_session_ex_clear(sess(ctx), lo, hi), "exclear failed");
}

int re_cmd_exlist(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_ex_rule_t rules[32];
    size_t n = 0, i;
    re_err_code_t c;
    re_jw_t w;
    (void)path;
    (void)argc;
    (void)argv;
    c = re_dbg_session_ex_list(sess(ctx), rules, 32, &n);
    if (c != RE_OK)
        return finish(ctx, "exlist", c, "exlist failed");
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "exlist");
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "count", n);
    re_jw_key(&w, "rules");
    re_jw_arr(&w);
    for (i = 0; i < n; i++) {
        re_jw_obj(&w);
        re_jw_ki64(&w, "lo", rules[i].code_lo);
        re_jw_ki64(&w, "hi", rules[i].code_hi);
        re_jw_kcstr(&w, "policy", re_dbg_ex_policy_str(rules[i].policy));
        re_jw_kbool(&w, "first_chance_only", rules[i].first_chance_only);
        re_jw_obj_end(&w);
    }
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_caps(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_caps_t caps = re_dbg_session_caps(sess(ctx));
    re_jw_t w;
    (void)path;
    (void)argc;
    (void)argv;
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "caps");
    re_jw_kbool(&w, "ok", true);
    re_jw_kcstr(&w, "backend", re_dbg_backend_name());
    re_jw_ku64(&w, "bits", caps.bits);
    re_jw_ku64(&w, "hw_slots", caps.hw_slots);
    re_jw_kbool(&w, "sw_bp", (caps.bits & RE_DBG_CAP_SW_BP) != 0);
    re_jw_kbool(&w, "hw_bp", (caps.bits & RE_DBG_CAP_HW_BP) != 0);
    re_jw_kbool(&w, "mem_bp", (caps.bits & RE_DBG_CAP_MEM_BP) != 0);
    re_jw_kbool(&w, "load_bp", (caps.bits & RE_DBG_CAP_LOAD_BP) != 0);
    re_jw_kbool(&w, "step", (caps.bits & RE_DBG_CAP_STEP) != 0);
    re_jw_kbool(&w, "pause", (caps.bits & RE_DBG_CAP_PAUSE) != 0);
    re_jw_kbool(&w, "attach", (caps.bits & RE_DBG_CAP_ATTACH) != 0);
    re_jw_kbool(&w, "modules", (caps.bits & RE_DBG_CAP_MODULES) != 0);
    re_jw_kbool(&w, "threads", (caps.bits & RE_DBG_CAP_THREADS) != 0);
    re_jw_kbool(&w, "exception_bp", (caps.bits & RE_DBG_CAP_EXCEPTION_BP) != 0);
    re_jw_kbool(&w, "signal_pass", (caps.bits & RE_DBG_CAP_SIGNAL_PASS) != 0);
    re_jw_kbool(&w, "first_chance", (caps.bits & RE_DBG_CAP_FIRST_CHANCE) != 0);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}
