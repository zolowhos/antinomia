// re_cmds_patch.c - assemble, patch list, and PE/ELF write-back commands.
// Module: cli (C11).
// Owns: a/asm, patchlist, patchwrite JSON reports on schema antinomia/1.
// Depends: re_cmds.h, re_dbg, re_patch, re_json, re_hex. stdout via re_jw_flush.
#include "cli/cmds/re_cmds.h"

#include "features/debug/re_dbg.h"
#include "features/patch/re_patch.h"
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

static const char *next_pos(int argc, char **argv, int skip) {
    int seen = 0, i;
    for (i = 0; i < argc; i++) {
        re_str_t a = re_str(argv[i]);
        if (re_str_starts_cstr(a, "--")) {
            i += 1;
            continue;
        }
        if (seen == skip)
            return argv[i];
        seen++;
    }
    return NULL;
}

static bool join_text(re_arena_t *a, int argc, char **argv, int from, char **out) {
    size_t need = 1, i;
    char *buf;
    size_t o = 0;
    int first = 1;
    for (i = (size_t)from; i < (size_t)argc; i++) {
        re_str_t s = re_str(argv[i]);
        if (re_str_starts_cstr(s, "--")) {
            i++;
            continue;
        }
        need += s.n + 1u;
    }
    buf = (char *)re_arena_alloc(a, need);
    if (!buf)
        return false;
    for (i = (size_t)from; i < (size_t)argc; i++) {
        re_str_t s = re_str(argv[i]);
        size_t k;
        if (re_str_starts_cstr(s, "--")) {
            i++;
            continue;
        }
        if (!first)
            buf[o++] = ' ';
        first = 0;
        for (k = 0; k < s.n; k++)
            buf[o++] = s.p[k];
    }
    buf[o] = 0;
    *out = buf;
    return o > 0;
}

int re_cmd_asm(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *addr_s = next_pos(argc, argv, 0);
    uint64_t addr = 0;
    char *text = NULL;
    size_t n = 0;
    re_err_code_t c;
    re_jw_t w;
    char hex[RE_PATCH_HEX_MAX];
    uint8_t bytes[RE_ASM_MAX_BYTES];
    re_dbg_session_t *s = sess(ctx);
    size_t i;
    int addr_i = -1;
    (void)path;
    if (!addr_s || !parse_u64(addr_s, &addr)) {
        emit_err(ctx, "a", RE_E_USAGE, "a <addr> <asm text...>");
        return 2;
    }
    for (i = 0; i < (size_t)argc; i++) {
        if (argv[i] == addr_s) {
            addr_i = (int)i;
            break;
        }
    }
    if (addr_i < 0 || !join_text(ctx->arena, argc, argv, addr_i + 1, &text)) {
        emit_err(ctx, "a", RE_E_USAGE, "a needs assembly text after the address");
        return 2;
    }
    c = re_dbg_session_assemble(s, addr, text, &n);
    if (c != RE_OK) {
        const char *msg = s->last_err.msg[0] ? s->last_err.msg : "assemble/patch failed";
        if (c == RE_E_MALFORMED)
            msg = "unsupported or malformed assembly";
        emit_err(ctx, "a", c, msg);
        return re_err_exit_code(c);
    }
    for (i = 0; i < s->patches.n; i++) {
        if (s->patches.items[i].addr == addr) {
            memcpy(bytes, s->patches.items[i].bytes, n);
            break;
        }
    }
    re_hex_encode(hex, bytes, n);
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "a");
    re_jw_kbool(&w, "ok", true);
    re_jw_khex(&w, "addr", addr, 16);
    re_jw_ku64(&w, "size", n);
    re_jw_kcstr(&w, "bytes", hex);
    re_jw_kcstr(&w, "text", text);
    re_jw_ku64(&w, "patch_count", s->patches.n);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_patchlist(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_session_t *s = sess(ctx);
    re_jw_t w;
    size_t i;
    (void)path;
    (void)argc;
    (void)argv;
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "patchlist");
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "count", s->patches.n);
    re_jw_key(&w, "patches");
    re_jw_arr(&w);
    for (i = 0; i < s->patches.n; i++) {
        const re_patch_entry_t *e = &s->patches.items[i];
        char oh[RE_PATCH_HEX_MAX];
        char ph[RE_PATCH_HEX_MAX];
        re_hex_encode(oh, e->orig, e->size);
        re_hex_encode(ph, e->bytes, e->size);
        re_jw_obj(&w);
        re_jw_khex(&w, "addr", e->addr, 16);
        re_jw_khex(&w, "rva", e->rva, 0);
        re_jw_ku64(&w, "size", e->size);
        re_jw_kcstr(&w, "orig", oh);
        re_jw_kcstr(&w, "patch", ph);
        re_jw_kcstr(&w, "text", e->text);
        if (e->module[0])
            re_jw_kcstr(&w, "module", e->module);
        re_jw_obj_end(&w);
    }
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_patchwrite(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *outp = next_pos(argc, argv, 0);
    size_t n_files = 0;
    re_err_code_t c;
    re_jw_t w;
    re_dbg_session_t *s = sess(ctx);
    (void)path;
    c = re_dbg_session_patch_writeback(s, outp, ctx->arena, &n_files);
    if (c != RE_OK) {
        emit_err(ctx, "patchwrite", c,
                 c == RE_E_NOTFOUND ? "no patches with a module path" : "write-back failed");
        return re_err_exit_code(c);
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "patchwrite");
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "files", n_files);
    re_jw_ku64(&w, "patches", s->patches.n);
    if (outp)
        re_jw_kcstr(&w, "out", outp);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}
