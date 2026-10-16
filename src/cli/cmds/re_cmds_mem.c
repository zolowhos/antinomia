// re_cmds_mem.c - paged memory page tools with hard limits and cursors.
// Module: cli (C11).
// Owns: mem.page / mem_page JSON dump with --limit/--offset/--cursor.
// Depends: re_cmds.h, re_dbg, re_json, re_hex. stdout via re_jw_flush only.
#include "cli/cmds/re_cmds.h"

#include "features/debug/re_dbg.h"
#include "utils/json/re_json.h"
#include "utils/text/re_hex.h"
#include "utils/text/re_str.h"
#include "utils/text/re_strbuf.h"

#include <string.h>

#define RE_MEM_PAGE_HARD_LIMIT 4096u
#define RE_MEM_PAGE_DEFAULT 256u

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

static bool parse_u64(const char *s, uint64_t *out) {
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

static void emit_cursor(re_jw_t *w, re_arena_t *a, size_t next, bool trunc) {
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

static bool resolve_base(re_ctx_t *ctx, const char *tool, int argc, char **argv, uint64_t *base) {
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
        if (!parse_u64(p, base)) {
            emit_err(ctx, tool, RE_E_USAGE, "mem.page needs a hex/decimal address");
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
        emit_err(ctx, tool, RE_E_USAGE, "mem.page needs an address when regs unavailable");
        return false;
    }
    *base = regs.rip;
    return true;
}

static size_t page_limit(const re_ctx_t *ctx) {
    size_t lim = ctx->limit ? ctx->limit : RE_MEM_PAGE_DEFAULT;
    if (ctx->len != 0 && ctx->len != 256)
        lim = (size_t)ctx->len;
    if (lim == 0)
        lim = RE_MEM_PAGE_DEFAULT;
    if (lim > RE_MEM_PAGE_HARD_LIMIT)
        lim = RE_MEM_PAGE_HARD_LIMIT;
    return lim;
}

static void emit_ok(re_ctx_t *ctx, const char *tool, uint64_t base, uint64_t addr, size_t off,
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
    emit_cursor(&w, ctx->arena, off + lim, true);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
}

static int do_mem_page(re_ctx_t *ctx, const char *tool, int argc, char **argv) {
    re_dbg_session_t *s = sess(ctx);
    uint64_t base = 0, addr;
    size_t lim, off;
    uint8_t *buf;
    char *hex;
    re_err_code_t c;
    const char *msg;
    if (!resolve_base(ctx, tool, argc, argv, &base))
        return 2;
    lim = page_limit(ctx);
    off = ctx->offset;
    addr = base + (uint64_t)off;
    buf = (uint8_t *)re_arena_alloc(ctx->arena, lim);
    hex = (char *)re_arena_alloc(ctx->arena, lim * 2u + 1u);
    if (!buf || !hex) {
        emit_err(ctx, tool, RE_E_NOMEM, "out of memory");
        return 6;
    }
    memset(buf, 0, lim);
    c = re_dbg_session_read_mem(s, addr, buf, lim);
    if (c != RE_OK) {
        msg = s->last_err.msg[0] ? s->last_err.msg : "memory page read failed";
        emit_err(ctx, tool, c, msg);
        return re_err_exit_code(c);
    }
    re_hex_encode(hex, buf, lim);
    emit_ok(ctx, tool, base, addr, off, lim, hex);
    return 0;
}

int re_cmd_mem_page(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    return do_mem_page(ctx, "mem.page", argc, argv);
}
