// re_cmds_inspect.c - regs, memory dump, and disasm command handlers.
// Module: cli (C11).
// Owns: regs/set_regs/d/u/mem_read/mem_write JSON emitters via session APIs.
// Depends: re_cmds.h, re_dbg, re_disasm, re_json, re_hex. stdout via re_jw_flush.
#include "cli/cmds/re_cmds.h"

#include "features/debug/re_dbg.h"
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
    re_jw_kbool(&w, "implemented", code != RE_E_UNSUPPORTED);
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

static bool parse_addr_arg(re_ctx_t *ctx, int argc, char **argv, uint64_t *addr, bool *have) {
    const char *p = re_cmd_positional(argc, argv, 0);
    *have = false;
    if (ctx->off != 0) {
        *addr = ctx->off;
        *have = true;
    }
    if (!p)
        return true;
    if (!parse_u64(p, addr))
        return false;
    *have = true;
    return true;
}

static void emit_regs(re_ctx_t *ctx, const char *tool, const re_dbg_regs_t *r) {
    re_dbg_session_t *s = sess(ctx);
    re_jw_t w;
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", tool);
    re_jw_kbool(&w, "ok", true);
    re_jw_kbool(&w, "attached", s->attached);
    re_jw_ku64(&w, "pid", s->pid);
    re_jw_ku64(&w, "tid", s->tid);
    re_jw_key(&w, "regs");
    re_jw_obj(&w);
    re_jw_khex(&w, "rax", r->rax, 16);
    re_jw_khex(&w, "rbx", r->rbx, 16);
    re_jw_khex(&w, "rcx", r->rcx, 16);
    re_jw_khex(&w, "rdx", r->rdx, 16);
    re_jw_khex(&w, "rsi", r->rsi, 16);
    re_jw_khex(&w, "rdi", r->rdi, 16);
    re_jw_khex(&w, "rbp", r->rbp, 16);
    re_jw_khex(&w, "rsp", r->rsp, 16);
    re_jw_khex(&w, "r8", r->r8, 16);
    re_jw_khex(&w, "r9", r->r9, 16);
    re_jw_khex(&w, "r10", r->r10, 16);
    re_jw_khex(&w, "r11", r->r11, 16);
    re_jw_khex(&w, "r12", r->r12, 16);
    re_jw_khex(&w, "r13", r->r13, 16);
    re_jw_khex(&w, "r14", r->r14, 16);
    re_jw_khex(&w, "r15", r->r15, 16);
    re_jw_khex(&w, "rip", r->rip, 16);
    re_jw_khex(&w, "rflags", r->rflags, 16);
    re_jw_obj_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
}

static bool set_one_reg(re_dbg_regs_t *r, re_str_t name, uint64_t v) {
    if (re_str_ieq_cstr(name, "rax"))
        r->rax = v;
    else if (re_str_ieq_cstr(name, "rbx"))
        r->rbx = v;
    else if (re_str_ieq_cstr(name, "rcx"))
        r->rcx = v;
    else if (re_str_ieq_cstr(name, "rdx"))
        r->rdx = v;
    else if (re_str_ieq_cstr(name, "rsi"))
        r->rsi = v;
    else if (re_str_ieq_cstr(name, "rdi"))
        r->rdi = v;
    else if (re_str_ieq_cstr(name, "rbp"))
        r->rbp = v;
    else if (re_str_ieq_cstr(name, "rsp") || re_str_ieq_cstr(name, "sp"))
        r->rsp = v;
    else if (re_str_ieq_cstr(name, "r8"))
        r->r8 = v;
    else if (re_str_ieq_cstr(name, "r9"))
        r->r9 = v;
    else if (re_str_ieq_cstr(name, "r10"))
        r->r10 = v;
    else if (re_str_ieq_cstr(name, "r11"))
        r->r11 = v;
    else if (re_str_ieq_cstr(name, "r12"))
        r->r12 = v;
    else if (re_str_ieq_cstr(name, "r13"))
        r->r13 = v;
    else if (re_str_ieq_cstr(name, "r14"))
        r->r14 = v;
    else if (re_str_ieq_cstr(name, "r15"))
        r->r15 = v;
    else if (re_str_ieq_cstr(name, "rip") || re_str_ieq_cstr(name, "ip"))
        r->rip = v;
    else if (re_str_ieq_cstr(name, "rflags") || re_str_ieq_cstr(name, "eflags") ||
             re_str_ieq_cstr(name, "flags"))
        r->rflags = v;
    else
        return false;
    return true;
}

static bool apply_reg_arg(re_dbg_regs_t *r, const char *arg) {
    re_str_t a = re_str(arg);
    size_t eq = 0;
    uint64_t v = 0;
    while (eq < a.n && a.p[eq] != '=')
        eq++;
    if (eq == 0 || eq >= a.n)
        return false;
    if (!parse_u64(a.p + eq + 1, &v))
        return false;
    return set_one_reg(r, re_strn(a.p, eq), v);
}

int re_cmd_regs(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_regs_t regs;
    re_err_code_t c;
    const char *msg;
    (void)path;
    (void)argc;
    (void)argv;
    memset(&regs, 0, sizeof(regs));
    c = re_dbg_session_get_regs(sess(ctx), 0, &regs);
    if (c != RE_OK) {
        msg = sess(ctx)->last_err.msg[0] ? sess(ctx)->last_err.msg : "regs failed";
        emit_err(ctx, "regs", c, msg);
        return re_err_exit_code(c);
    }
    emit_regs(ctx, "regs", &regs);
    return 0;
}

int re_cmd_set_regs(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_regs_t regs;
    re_err_code_t c;
    int applied = 0;
    const char *msg;
    (void)path;
    memset(&regs, 0, sizeof(regs));
    c = re_dbg_session_get_regs(sess(ctx), 0, &regs);
    if (c != RE_OK) {
        msg = sess(ctx)->last_err.msg[0] ? sess(ctx)->last_err.msg : "set_regs failed";
        emit_err(ctx, "set_regs", c, msg);
        return re_err_exit_code(c);
    }
    for (int i = 0; i < argc; i++) {
        re_str_t a = re_str(argv[i]);
        if (re_str_starts_cstr(a, "--"))
            continue;
        if (!apply_reg_arg(&regs, argv[i])) {
            emit_err(ctx, "set_regs", RE_E_USAGE, "set_regs expects name=value pairs");
            return 2;
        }
        applied++;
    }
    if (applied == 0) {
        emit_err(ctx, "set_regs", RE_E_USAGE, "set_regs needs at least one name=value");
        return 2;
    }
    c = re_dbg_session_set_regs(sess(ctx), 0, &regs);
    if (c != RE_OK) {
        msg = sess(ctx)->last_err.msg[0] ? sess(ctx)->last_err.msg : "set_regs failed";
        emit_err(ctx, "set_regs", c, msg);
        return re_err_exit_code(c);
    }
    emit_regs(ctx, "set_regs", &regs);
    return 0;
}

static int emit_dump(re_ctx_t *ctx, const char *tool, uint64_t addr, size_t len) {
    uint8_t *buf;
    char *hex;
    re_err_code_t c;
    re_jw_t w;
    const char *msg;
    if (len == 0)
        len = 16;
    if (len > 4096)
        len = 4096;
    buf = (uint8_t *)re_arena_alloc(ctx->arena, len);
    hex = (char *)re_arena_alloc(ctx->arena, len * 2u + 1u);
    if (!buf || !hex) {
        emit_err(ctx, tool, RE_E_NOMEM, "out of memory");
        return 6;
    }
    memset(buf, 0, len);
    c = re_dbg_session_read_mem(sess(ctx), addr, buf, len);
    if (c != RE_OK) {
        msg = sess(ctx)->last_err.msg[0] ? sess(ctx)->last_err.msg : "memory read failed";
        emit_err(ctx, tool, c, msg);
        return re_err_exit_code(c);
    }
    re_hex_encode(hex, buf, len);
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", tool);
    re_jw_kbool(&w, "ok", true);
    re_jw_khex(&w, "addr", addr, 16);
    re_jw_ku64(&w, "len", len);
    re_jw_kcstr(&w, "hex", hex);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_dump(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    uint64_t addr = 0;
    bool have = false;
    re_dbg_regs_t regs;
    size_t len = ctx->len ? (size_t)ctx->len : 16;
    (void)path;
    if (!parse_addr_arg(ctx, argc, argv, &addr, &have)) {
        emit_err(ctx, "d", RE_E_USAGE, "d needs a hex/decimal address");
        return 2;
    }
    if (!have) {
        memset(&regs, 0, sizeof(regs));
        if (re_dbg_session_get_regs(sess(ctx), 0, &regs) != RE_OK) {
            emit_err(ctx, "d", RE_E_USAGE, "d needs an address when regs are unavailable");
            return 2;
        }
        addr = regs.rip;
    }
    return emit_dump(ctx, "d", addr, len);
}

int re_cmd_mem_read(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    uint64_t addr = 0;
    bool have = false;
    size_t len = ctx->len ? (size_t)ctx->len : 16;
    (void)path;
    if (!parse_addr_arg(ctx, argc, argv, &addr, &have) || !have) {
        emit_err(ctx, "mem_read", RE_E_USAGE, "mem_read needs an address");
        return 2;
    }
    return emit_dump(ctx, "mem_read", addr, len);
}

static const char *next_positional(int argc, char **argv, int skip) {
    int seen = 0;
    for (int i = 0; i < argc; i++) {
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

int re_cmd_mem_write(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    uint64_t addr = 0;
    const char *addr_s = re_cmd_positional(argc, argv, 0);
    const char *hex_s;
    uint8_t *buf;
    size_t nbytes = 0;
    re_err_code_t c;
    re_jw_t w;
    const char *msg;
    re_str_t hs;
    (void)path;
    if (addr_s && parse_u64(addr_s, &addr)) {
        hex_s = next_positional(argc, argv, 1);
    } else if (ctx->off != 0) {
        addr = ctx->off;
        hex_s = next_positional(argc, argv, 0);
    } else {
        emit_err(ctx, "mem_write", RE_E_USAGE, "mem_write needs addr and hex bytes");
        return 2;
    }
    if (!hex_s) {
        emit_err(ctx, "mem_write", RE_E_USAGE, "mem_write needs hex bytes after the address");
        return 2;
    }
    hs = re_str(hex_s);
    buf = (uint8_t *)re_arena_alloc(ctx->arena, hs.n / 2u + 1u);
    if (!buf) {
        emit_err(ctx, "mem_write", RE_E_NOMEM, "out of memory");
        return 6;
    }
    if (!re_hex_decode(buf, hs.n / 2u + 1u, hs.p, hs.n, &nbytes, true) || nbytes == 0) {
        emit_err(ctx, "mem_write", RE_E_USAGE, "mem_write hex bytes are malformed");
        return 2;
    }
    c = re_dbg_session_write_mem(sess(ctx), addr, buf, nbytes);
    if (c != RE_OK) {
        msg = sess(ctx)->last_err.msg[0] ? sess(ctx)->last_err.msg : "memory write failed";
        emit_err(ctx, "mem_write", c, msg);
        return re_err_exit_code(c);
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "mem_write");
    re_jw_kbool(&w, "ok", true);
    re_jw_khex(&w, "addr", addr, 16);
    re_jw_ku64(&w, "len", nbytes);
    re_jw_kbool(&w, "disasm_cache_invalidated", true);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

static void emit_insn_obj(re_jw_t *w, const re_disasm_insn_t *in) {
    char hex[33];
    re_hex_encode(hex, in->bytes, in->size);
    re_jw_obj(w);
    re_jw_khex(w, "addr", in->addr, 16);
    re_jw_ku64(w, "size", in->size);
    re_jw_kcstr(w, "bytes", hex);
    re_jw_kcstr(w, "text", in->text);
    if (in->comment[0])
        re_jw_kcstr(w, "comment", in->comment);
    if (in->flags & (RE_DISASM_F_CALL | RE_DISASM_F_JMP | RE_DISASM_F_JCC))
        re_jw_khex(w, "target", in->target, 16);
    re_jw_obj_end(w);
}

int re_cmd_disasm(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    uint64_t addr = 0;
    bool have = false;
    re_dbg_regs_t regs;
    re_disasm_insn_t lines[RE_DISASM_CACHE_N];
    size_t n = 0, i;
    size_t limit = ctx->limit ? ctx->limit : 8;
    re_err_code_t c;
    re_jw_t w;
    const char *msg;
    (void)path;
    if (!parse_addr_arg(ctx, argc, argv, &addr, &have)) {
        emit_err(ctx, "u", RE_E_USAGE, "u needs a hex/decimal address");
        return 2;
    }
    if (!have) {
        memset(&regs, 0, sizeof(regs));
        c = re_dbg_session_get_regs(sess(ctx), 0, &regs);
        if (c != RE_OK) {
            msg =
                sess(ctx)->last_err.msg[0] ? sess(ctx)->last_err.msg : "u needs RIP or an address";
            emit_err(ctx, "u", c, msg);
            return re_err_exit_code(c);
        }
        addr = regs.rip;
    }
    if (limit > RE_DISASM_CACHE_N)
        limit = RE_DISASM_CACHE_N;
    c = re_dbg_session_disasm(sess(ctx), addr, limit, lines, &n);
    if (c != RE_OK) {
        msg = sess(ctx)->last_err.msg[0] ? sess(ctx)->last_err.msg : "disasm failed";
        emit_err(ctx, "u", c, msg);
        return re_err_exit_code(c);
    }
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "u");
    re_jw_kbool(&w, "ok", true);
    re_jw_khex(&w, "addr", addr, 16);
    re_jw_ku64(&w, "count", n);
    re_jw_kbool(&w, "truncated", n >= limit);
    re_jw_key(&w, "insns");
    re_jw_arr(&w);
    for (i = 0; i < n; i++)
        emit_insn_obj(&w, &lines[i]);
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}
