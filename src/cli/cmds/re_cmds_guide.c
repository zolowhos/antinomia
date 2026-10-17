// re_cmds_guide.c - Phase 3 guided triage and honest ida-mcp shims.
// Module: cli (C11).
// Owns: triage-live report and ida-mcp-shaped tools that never fake results.
// Depends: re_cmds.h, re_dbg, re_buf, re_json. stdout only via re_jw_flush.
#include "cli/cmds/re_cmds.h"

#include "features/debug/re_dbg.h"
#include "features/persist/re_dbg_db.h"
#include "utils/json/re_json.h"
#include "utils/mem/re_buf.h"
#include "utils/text/re_hex.h"
#include "utils/text/re_str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#    include <unistd.h>
#endif

#define RE_TRIAGE_MOD_MAX 256u
#define RE_TRIAGE_IMPORT_MAX 24u

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

static void emit_stub(re_ctx_t *ctx, const char *tool, const char *msg, const char *maps_to) {
    re_jw_t w;
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", tool);
    re_jw_kbool(&w, "ok", false);
    re_jw_kbool(&w, "implemented", false);
    re_jw_kcstr(&w, "shim", "ida-mcp");
    if (maps_to && maps_to[0])
        re_jw_kcstr(&w, "maps_to", maps_to);
    re_jw_key(&w, "error");
    re_jw_obj(&w);
    re_jw_kcstr(&w, "code", re_err_json_code(RE_E_UNSUPPORTED));
    re_jw_kcstr(&w, "message", msg);
    re_jw_obj_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    if (ctx->err)
        RE_ERR_SET(ctx->err, RE_E_UNSUPPORTED, msg);
}

static bool elf_entry(re_span_t file, uint64_t base, uint64_t *entry_out) {
    uint8_t cls;
    uint64_t e_entry = 0;
    uint32_t e32 = 0;
    if (file.n < 64 || file.p[0] != 0x7f || file.p[1] != 'E' || file.p[2] != 'L' ||
        file.p[3] != 'F')
        return false;
    cls = file.p[4];
    if (cls == 2) {
        if (!re_rd64(file, 24, &e_entry))
            return false;
        *entry_out = base + e_entry;
        return true;
    }
    if (cls == 1) {
        if (!re_rd32(file, 24, &e32))
            return false;
        *entry_out = base + e32;
        return true;
    }
    return false;
}

static bool pe_entry(re_span_t file, uint64_t base, uint64_t *entry_out) {
    uint32_t lfanew = 0, ep_rva = 0;
    if (file.n < 0x40 || file.p[0] != 'M' || file.p[1] != 'Z')
        return false;
    if (!re_rd32(file, 0x3c, &lfanew) || (uint64_t)lfanew + 0x28 >= file.n)
        return false;
    if (!re_rd32(file, (uint64_t)lfanew + 0x28, &ep_rva))
        return false;
    *entry_out = base + ep_rva;
    return true;
}

static bool file_entry(re_arena_t *a, const char *path, uint64_t base, uint64_t *entry_out,
                       const char **fmt_out) {
    re_file_t f;
    bool ok = false;
    *entry_out = 0;
    *fmt_out = "unknown";
    if (!path || !path[0] || !a)
        return false;
    if (re_file_open(path, a, &f) != RE_OK)
        return false;
    if (elf_entry(f.whole, base, entry_out)) {
        *fmt_out = "elf";
        ok = true;
    } else if (pe_entry(f.whole, base, entry_out)) {
        *fmt_out = "pe";
        ok = true;
    }
    re_file_close(&f);
    return ok;
}

static const char *const kInteresting[] = {
    "IsDebuggerPresent",
    "CheckRemoteDebuggerPresent",
    "NtQueryInformationProcess",
    "OutputDebugStringA",
    "GetTickCount",
    "VirtualProtect",
    "LoadLibraryA",
    "GetProcAddress",
    "CreateRemoteThread",
    "WriteProcessMemory",
    "ptrace",
    "dlopen",
    "getenv",
    "socket",
    "connect",
    "fopen",
    "malloc",
    NULL,
};

static void emit_interesting(re_jw_t *w, re_dbg_session_t *s) {
    size_t i, n = 0;
    re_jw_key(w, "interesting_imports");
    re_jw_arr(w);
    for (i = 0; kInteresting[i] && n < RE_TRIAGE_IMPORT_MAX; i++) {
        uint64_t addr = 0;
        re_dbg_symbol_t info;
        memset(&info, 0, sizeof(info));
        if (re_dbg_session_resolve(s, kInteresting[i], &addr, &info) != RE_OK)
            continue;
        re_jw_obj(w);
        re_jw_kcstr(w, "name", kInteresting[i]);
        re_jw_khex(w, "addr", addr, 16);
        re_jw_kcstr(w, "module", info.module[0] ? info.module : "");
        re_jw_obj_end(w);
        n++;
    }
    re_jw_arr_end(w);
    re_jw_ku64(w, "interesting_count", n);
}

static void ad_session(re_jw_t *w, re_dbg_session_t *s) {
    re_jw_obj(w);
    re_jw_kcstr(w, "check", "session_attached");
    re_jw_kbool(w, "present", s->attached);
    re_jw_kcstr(w, "detail", s->attached ? "debug session owns the target" : "no live target");
    re_jw_obj_end(w);
}

static void ad_ptrace_export(re_jw_t *w, re_dbg_session_t *s) {
    uint64_t addr = 0;
    if (re_dbg_session_resolve(s, "ptrace", &addr, NULL) != RE_OK)
        return;
    re_jw_obj(w);
    re_jw_kcstr(w, "check", "ptrace_export");
    re_jw_kbool(w, "present", true);
    re_jw_khex(w, "addr", addr, 16);
    re_jw_kcstr(w, "detail", "libc ptrace visible; common anti-debug probe");
    re_jw_obj_end(w);
}

#if !defined(_WIN32)
static void ad_tracer_pid(re_jw_t *w, re_dbg_session_t *s) {
    char path[64];
    FILE *fp;
    char line[256];
    uint32_t tracer = 0;
    bool found = false;
    if (!s->attached || !s->pid)
        return;
    snprintf(path, sizeof(path), "/proc/%u/status", (unsigned)s->pid);
    fp = fopen(path, "rb");
    if (!fp)
        return;
    while (fgets(line, (int)sizeof(line), fp)) {
        re_str_t row = re_str(line);
        if (!re_str_starts_cstr(row, "TracerPid:"))
            continue;
        tracer = (uint32_t)strtoul(line + 10, NULL, 10);
        found = true;
        break;
    }
    fclose(fp);
    re_jw_obj(w);
    re_jw_kcstr(w, "check", "TracerPid");
    re_jw_kbool(w, "present", found);
    if (found) {
        re_jw_ku64(w, "value", tracer);
        re_jw_kbool(w, "matches_debugger", tracer == (uint32_t)getpid());
    }
    re_jw_kcstr(w, "detail", "from /proc/<pid>/status");
    re_jw_obj_end(w);
}
#else
static void ad_is_debugger(re_jw_t *w, re_dbg_session_t *s) {
    uint64_t addr = 0;
    re_jw_obj(w);
    re_jw_kcstr(w, "check", "IsDebuggerPresent");
    if (re_dbg_session_resolve(s, "IsDebuggerPresent", &addr, NULL) == RE_OK) {
        re_jw_kbool(w, "present", true);
        re_jw_khex(w, "addr", addr, 16);
        re_jw_kcstr(w, "detail", "export resolved; PEB.BeingDebugged not read");
    } else {
        re_jw_kbool(w, "present", false);
        re_jw_kcstr(w, "detail", "export not found in loaded modules");
    }
    re_jw_obj_end(w);
}
#endif

static void emit_anti_debug(re_jw_t *w, re_dbg_session_t *s) {
    re_jw_key(w, "anti_debug");
    re_jw_arr(w);
    ad_session(w, s);
#if !defined(_WIN32)
    ad_tracer_pid(w, s);
#else
    ad_is_debugger(w, s);
#endif
    ad_ptrace_export(w, s);
    re_jw_arr_end(w);
}

static void emit_modules(re_jw_t *w, const re_dbg_module_t *mods, size_t nmod, size_t lim) {
    size_t i;
    re_jw_key(w, "modules");
    re_jw_arr(w);
    for (i = 0; i < lim; i++) {
        re_jw_obj(w);
        re_jw_khex(w, "base", mods[i].base, 16);
        re_jw_ku64(w, "size", mods[i].size);
        re_jw_kcstr(w, "path", mods[i].path);
        re_jw_kcstr(w, "build_id", mods[i].build_id);
        re_jw_obj_end(w);
    }
    re_jw_arr_end(w);
    re_jw_ku64(w, "module_count", nmod);
    re_jw_kbool(w, "modules_truncated", lim < nmod);
}

static void emit_entry(re_jw_t *w, const re_dbg_regs_t *regs, const re_dbg_module_t *main_mod,
                       bool have_ep, uint64_t file_ep, const char *fmt) {
    re_jw_key(w, "entry");
    re_jw_obj(w);
    re_jw_khex(w, "rip", regs->rip, 16);
    re_jw_khex(w, "rsp", regs->rsp, 16);
    re_jw_kbool(w, "file_entry_ok", have_ep);
    re_jw_kcstr(w, "image_format", fmt);
    if (have_ep)
        re_jw_khex(w, "file_entry", file_ep, 16);
    if (main_mod) {
        re_jw_khex(w, "main_base", main_mod->base, 16);
        re_jw_kcstr(w, "main_path", main_mod->path);
    }
    re_jw_obj_end(w);
}

int re_cmd_triage_live(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_session_t *s = sess(ctx);
    re_dbg_module_t mods[RE_TRIAGE_MOD_MAX];
    re_dbg_regs_t regs;
    re_jw_t w;
    size_t nmod = 0, lim;
    uint64_t file_ep = 0;
    const char *fmt = "unknown";
    bool have_ep = false;
    (void)path;
    (void)argc;
    (void)argv;
    memset(&regs, 0, sizeof(regs));
    if (!s->attached) {
        emit_err(ctx, "triage-live", RE_E_USAGE, "triage-live requires an attached target");
        return 2;
    }
    if (re_dbg_session_enum_modules(s, mods, RE_TRIAGE_MOD_MAX, &nmod) != RE_OK) {
        emit_err(ctx, "triage-live", RE_E_IO, "modules enum failed");
        return re_err_exit_code(RE_E_IO);
    }
    (void)re_dbg_session_get_regs(s, 0, &regs);
    if (nmod > 0)
        have_ep = file_entry(ctx->arena, mods[0].path, mods[0].base, &file_ep, &fmt);
    lim = nmod;
    if (ctx->limit && lim > ctx->limit)
        lim = ctx->limit;
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "triage-live");
    re_jw_kbool(&w, "ok", true);
    re_jw_ku64(&w, "pid", s->pid);
    re_jw_ku64(&w, "tid", s->tid);
    re_jw_kcstr(&w, "path", s->path);
    re_jw_kcstr(&w, "backend", re_dbg_backend_name());
    emit_entry(&w, &regs, nmod > 0 ? &mods[0] : NULL, have_ep, file_ep, fmt);
    emit_modules(&w, mods, nmod, lim);
    emit_interesting(&w, s);
    emit_anti_debug(&w, s);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_ida_open_database(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *file = path && path[0] ? path : re_cmd_positional(argc, argv, 0);
    re_err_code_t c;
    re_jw_t w;
    if (!file || !file[0]) {
        emit_err(ctx, "open_database", RE_E_USAGE, "open_database needs a path (maps to init)");
        return 2;
    }
    c = re_dbg_session_launch(sess(ctx), file, NULL);
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "open_database");
    re_jw_kbool(&w, "ok", c == RE_OK);
    re_jw_kbool(&w, "implemented", c != RE_E_UNSUPPORTED);
    re_jw_kcstr(&w, "shim", "ida-mcp");
    re_jw_kcstr(&w, "maps_to", "init");
    re_jw_kcstr(&w, "path", file);
    re_jw_kcstr(&w, "note", "launches a live debug target; not an IDA database");
    if (c != RE_OK) {
        re_jw_key(&w, "error");
        re_jw_obj(&w);
        re_jw_kcstr(&w, "code", re_err_json_code(c));
        re_jw_kcstr(&w, "message",
                    sess(ctx)->last_err.msg[0] ? sess(ctx)->last_err.msg : re_err_str(c));
        re_jw_obj_end(&w);
    } else {
        re_jw_kbool(&w, "attached", sess(ctx)->attached);
        re_jw_ku64(&w, "pid", sess(ctx)->pid);
    }
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    if (ctx->err && c != RE_OK)
        RE_ERR_SET(ctx->err, c, "open_database failed");
    return re_err_exit_code(c);
}

static bool parse_u64_addr(const char *s, uint64_t *out) {
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

int re_cmd_ida_get_bytes(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    const char *addr_s = re_cmd_positional(argc, argv, 0);
    uint64_t addr = 0;
    size_t len = ctx->len ? (size_t)ctx->len : 16;
    uint8_t *buf;
    char *hex;
    re_err_code_t c;
    re_jw_t w;
    (void)path;
    if (!addr_s || !parse_u64_addr(addr_s, &addr)) {
        emit_err(ctx, "get_bytes", RE_E_USAGE, "get_bytes needs an address (maps to mem_read)");
        return 2;
    }
    if (len > 4096)
        len = 4096;
    buf = (uint8_t *)re_arena_alloc(ctx->arena, len);
    hex = (char *)re_arena_alloc(ctx->arena, len * 2u + 1u);
    if (!buf || !hex) {
        emit_err(ctx, "get_bytes", RE_E_NOMEM, "out of memory");
        return 6;
    }
    memset(buf, 0, len);
    c = re_dbg_session_read_mem(sess(ctx), addr, buf, len);
    if (c != RE_OK) {
        emit_err(ctx, "get_bytes", c, "memory read failed");
        return re_err_exit_code(c);
    }
    re_hex_encode(hex, buf, len);
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "get_bytes");
    re_jw_kbool(&w, "ok", true);
    re_jw_kbool(&w, "implemented", true);
    re_jw_kcstr(&w, "shim", "ida-mcp");
    re_jw_kcstr(&w, "maps_to", "mem_read");
    re_jw_khex(&w, "addr", addr, 16);
    re_jw_ku64(&w, "len", len);
    re_jw_kcstr(&w, "hex", hex);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_ida_list_funcs(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_dbg_db_t *db = &sess(ctx)->db;
    re_jw_t w;
    size_t i;
    (void)path;
    (void)argc;
    (void)argv;
    re_jw_init(&w, ctx->arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "tool", "list_funcs");
    re_jw_kbool(&w, "ok", true);
    re_jw_kbool(&w, "implemented", true);
    re_jw_kcstr(&w, "shim", "ida-mcp");
    re_jw_kcstr(&w, "maps_to", "funcs");
    re_jw_kcstr(&w, "note", "session function cache only; not IDA auto-analysis");
    re_jw_ku64(&w, "count", db->n_funcs);
    re_jw_key(&w, "functions");
    re_jw_arr(&w);
    for (i = 0; i < db->n_funcs; i++) {
        re_jw_obj(&w);
        re_jw_khex(&w, "start", db->funcs[i].start_rva, 0);
        re_jw_khex(&w, "end", db->funcs[i].end_rva, 0);
        re_jw_kcstr(&w, "name", db->funcs[i].name);
        re_jw_obj_end(&w);
    }
    re_jw_arr_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, re_ctx_out(ctx));
    return 0;
}

int re_cmd_ida_decompile(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    emit_stub(ctx, "decompile", "no decompiler; refuse to invent pseudocode (use u/graph)", "");
    return re_err_exit_code(RE_E_UNSUPPORTED);
}

int re_cmd_ida_get_xrefs_to(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    emit_stub(ctx, "get_xrefs_to", "cross-reference index not implemented; no fake xrefs", "");
    return re_err_exit_code(RE_E_UNSUPPORTED);
}

int re_cmd_ida_execute_query(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    emit_stub(ctx, "execute_query",
              "no IDC/IDAPython; use named antinomia tools (bp, state, triage-live)", "script");
    return re_err_exit_code(RE_E_UNSUPPORTED);
}
