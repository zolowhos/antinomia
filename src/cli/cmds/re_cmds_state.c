// re_cmds_state.c - agent-safe session snapshot and paged memory dump.
// Module: cli (C11).
// Owns: state + mem.page tools (regs/disasm/BPs/log; paged mem with cursors).
// Depends: re_cmds.h, re_dbg, re_dbg_advbp, re_json, re_hex. stdout via re_jw_flush.
#include "cli/cmds/re_cmds.h"

#include "features/bp/re_dbg_advbp.h"
#include "features/debug/re_dbg.h"
#include "features/script/re_script.h"
#include "utils/json/re_json.h"
#include "utils/text/re_hex.h"
#include "utils/text/re_str.h"
#include "utils/text/re_strbuf.h"

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
            emit_err(ctx, "state", rc, s->last_err.msg[0] ? s->last_err.msg : "state regs failed");
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

/* --- mem.page (merged from re_cmds_mem.c) --- */

#define RE_MEM_PAGE_HARD_LIMIT 4096u
#define RE_MEM_PAGE_DEFAULT 256u

static void mp_emit_err(re_ctx_t *ctx, const char *tool, re_err_code_t code, const char *msg) {
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

static bool mp_parse_u64(const char *s, uint64_t *out) {
    re_str_t v;
    uint64_t base = 10, acc = 0;
    size_t i = 0;
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

static void mp_emit_cursor(re_jw_t *w, re_arena_t *a, size_t next, bool trunc) {
    re_strbuf_t cur;
    re_jw_kbool(w, "truncated", trunc);
    if (!trunc) {
        re_jw_knull(w, "next_offset");
        re_jw_knull(w, "next_cursor");
        return;
    }
    re_jw_ku64(w, "next_offset", next);
    re_strbuf_init(&cur, a);
    re_strbuf_put_u64(&cur, next);
    re_jw_kcstr(w, "next_cursor", cur.p ? cur.p : "");
}

static bool mp_resolve_base(re_ctx_t *ctx, const char *tool, int argc, char **argv,
                            uint64_t *base) {
    re_dbg_session_t *s = sess(ctx);
    re_dbg_regs_t regs;
    const char *p = re_cmd_positional(argc, argv, 0);
    bool have = false;
    *base = 0;
    if (ctx->off != 0) {
        *base = ctx->off;
        have = true;
    }
    if (p) {
        if (!mp_parse_u64(p, base)) {
            mp_emit_err(ctx, tool, RE_E_USAGE, "mem.page needs a hex/decimal address");
            return false;
        }
        return true;
    }
    if (have)
        return true;
    if (s->follow_dump) {
        *base = s->follow_dump;
        return true;
    }
    memset(&regs, 0, sizeof(regs));
    if (re_dbg_session_get_regs(s, 0, &regs) != RE_OK) {
        mp_emit_err(ctx, tool, RE_E_USAGE, "mem.page needs an address when regs unavailable");
        return false;
    }
    *base = regs.rip;
    return true;
}

static size_t mp_page_limit(const re_ctx_t *ctx) {
    size_t lim = ctx->limit ? ctx->limit : RE_MEM_PAGE_DEFAULT;
    if (ctx->len != 0 && ctx->len != 256)
        lim = (size_t)ctx->len;
    if (lim == 0)
        lim = RE_MEM_PAGE_DEFAULT;
    if (lim > RE_MEM_PAGE_HARD_LIMIT)
        lim = RE_MEM_PAGE_HARD_LIMIT;
    return lim;
}

static void mp_emit_ok(re_ctx_t *ctx, const char *tool, uint64_t base, uint64_t addr, size_t off,
                       size_t lim, const char *hex) {
    re_jw_t w;
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", tool);
    re_jw_kbool(&w, "ok", true);
    re_jw_khex(&w, "base", base, 16);
    re_jw_khex(&w, "addr", addr, 16);
    re_jw_ku64(&w, "offset", off);
    re_jw_ku64(&w, "limit", lim);
    re_jw_ku64(&w, "hard_limit", RE_MEM_PAGE_HARD_LIMIT);
    re_jw_ku64(&w, "len", lim);
    re_jw_kcstr(&w, "hex", hex);
    /* Successful page always offers a next cursor; agents stop when read fails. */
    mp_emit_cursor(&w, ctx->arena, off + lim, true);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
}

static int mp_do_mem_page(re_ctx_t *ctx, const char *tool, int argc, char **argv) {
    re_dbg_session_t *s = sess(ctx);
    uint64_t base = 0, addr;
    size_t lim, off;
    uint8_t *buf;
    char *hex;
    re_err_code_t c;
    const char *msg;
    if (!mp_resolve_base(ctx, tool, argc, argv, &base))
        return 2;
    lim = mp_page_limit(ctx);
    off = ctx->offset;
    addr = base + (uint64_t)off;
    buf = (uint8_t *)re_arena_alloc(ctx->arena, lim);
    hex = (char *)re_arena_alloc(ctx->arena, lim * 2u + 1u);
    if (!buf || !hex) {
        mp_emit_err(ctx, tool, RE_E_NOMEM, "out of memory");
        return 6;
    }
    memset(buf, 0, lim);
    c = re_dbg_session_read_mem(s, addr, buf, lim);
    if (c != RE_OK) {
        msg = s->last_err.msg[0] ? s->last_err.msg : "memory page read failed";
        mp_emit_err(ctx, tool, c, msg);
        return re_err_exit_code(c);
    }
    re_hex_encode(hex, buf, lim);
    mp_emit_ok(ctx, tool, base, addr, off, lim, hex);
    return 0;
}

int re_cmd_mem_page(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    return mp_do_mem_page(ctx, "mem.page", argc, argv);
}
