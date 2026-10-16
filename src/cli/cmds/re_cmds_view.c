// re_cmds_view.c - graph/CFG and memory find/search command handlers.
// Module: cli (C11).
// Owns: graph/cfg and find/search JSON emitters on schema antinomia/1.
// Depends: re_cmds.h, re_disasm_cfg, re_dbg_find, re_json. stdout via re_jw_flush.
#include "cli/cmds/re_cmds.h"

#include "features/disasm/re_disasm_cfg.h"
#include "features/search/re_dbg_find.h"
#include "utils/json/re_json.h"
#include "utils/text/re_hex.h"
#include "utils/text/re_str.h"
#include "utils/text/re_strbuf.h"

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

static bool parse_u64(const char *s, uint64_t *out) {
    re_str_t v;
    uint64_t base = 10;
    size_t i = 0;
    uint64_t acc = 0;
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

static char *join_pattern(re_arena_t *a, int argc, char **argv) {
    re_strbuf_t b;
    int i;
    bool first = true;
    re_strbuf_init(&b, a);
    for (i = 0; i < argc; i++) {
        re_str_t arg = re_str(argv[i]);
        if (re_str_starts_cstr(arg, "--")) {
            i += 1;
            continue;
        }
        if (!first)
            re_strbuf_putc(&b, ' ');
        re_strbuf_puts(&b, argv[i]);
        first = false;
    }
    return b.len ? b.p : NULL;
}

int re_cmd_graph(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    uint64_t entry = 0;
    const char *p = re_cmd_positional(argc, argv, 0);
    re_cfg_t g;
    re_err_code_t c;
    re_jw_t w;
    size_t i;
    (void)path;
    if (ctx->off)
        entry = ctx->off;
    if (p && !parse_u64(p, &entry)) {
        emit_err(ctx, "graph", RE_E_USAGE, "graph needs a hex/decimal address");
        return 2;
    }
    c = re_dbg_session_cfg(sess(ctx), entry, &g);
    if (c != RE_OK) {
        emit_err(ctx, "graph", c,
                 c == RE_E_USAGE ? "graph needs an attached target" : "cfg failed");
        return re_err_exit_code(c);
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "graph");
    re_jw_kbool(&w, "ok", true);
    re_jw_khex(&w, "entry", g.entry, 16);
    re_jw_kbool(&w, "truncated", g.truncated);
    re_jw_ku64(&w, "block_count", g.n_blocks);
    re_jw_ku64(&w, "edge_count", g.n_edges);
    re_jw_key(&w, "blocks");
    re_jw_arr(&w);
    for (i = 0; i < g.n_blocks; i++) {
        re_jw_obj(&w);
        re_jw_khex(&w, "start", g.blocks[i].start, 16);
        re_jw_khex(&w, "end", g.blocks[i].end, 16);
        re_jw_ku64(&w, "insns", g.blocks[i].insn_count);
        re_jw_kbool(&w, "ret", g.blocks[i].is_ret);
        re_jw_kbool(&w, "call", g.blocks[i].is_call);
        re_jw_obj_end(&w);
    }
    re_jw_arr_end(&w);
    re_jw_key(&w, "edges");
    re_jw_arr(&w);
    for (i = 0; i < g.n_edges; i++) {
        re_jw_obj(&w);
        re_jw_khex(&w, "from", g.edges[i].from, 16);
        re_jw_khex(&w, "to", g.edges[i].to, 16);
        re_jw_kcstr(&w, "kind", g.edges[i].kind);
        re_jw_obj_end(&w);
    }
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

static int do_find(re_ctx_t *ctx, const char *tool, int argc, char **argv) {
    char *spec;
    re_dbg_find_pat_t pat;
    re_dbg_find_hit_t hits[RE_FIND_MAX_HITS];
    size_t n = 0, i, start_i, lim;
    bool trunc = false;
    re_err_code_t c;
    re_jw_t w;
    uint64_t start = ctx->off;
    uint64_t len = ctx->len;
    spec = join_pattern(ctx->arena, argc, argv);
    if (!spec) {
        emit_err(ctx, tool, RE_E_USAGE, "find needs a pattern (hex, \"str\", or u32:val)");
        return 2;
    }
    c = re_dbg_find_parse(spec, &pat);
    if (c != RE_OK) {
        emit_err(ctx, tool, RE_E_USAGE, "malformed find pattern");
        return 2;
    }
    /* Default len from flag parser is 256; treat lone --off as module-wide. */
    if (start == 0 && len == 256)
        len = 0;
    c = re_dbg_session_find(sess(ctx), start, len, &pat, hits, RE_FIND_MAX_HITS, &n, &trunc);
    if (c != RE_OK) {
        emit_err(ctx, tool, c, c == RE_E_USAGE ? "find needs an attached target" : "find failed");
        return re_err_exit_code(c);
    }
    start_i = ctx->offset;
    if (start_i > n)
        start_i = n;
    lim = n - start_i;
    if (lim > ctx->limit)
        lim = ctx->limit;
    if (start_i + lim < n)
        trunc = true;
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", tool);
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "pattern_len", pat.n);
    re_jw_ku64(&w, "count", n);
    re_jw_kbool(&w, "truncated", trunc);
    re_jw_key(&w, "hits");
    re_jw_arr(&w);
    for (i = 0; i < lim; i++) {
        re_jw_obj(&w);
        re_jw_khex(&w, "addr", hits[start_i + i].addr, 16);
        re_jw_obj_end(&w);
    }
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_mem_find(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    return do_find(ctx, "find", argc, argv);
}

int re_cmd_search(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    return do_find(ctx, "search", argc, argv);
}
