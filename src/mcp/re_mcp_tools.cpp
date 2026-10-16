// re_mcp_tools.cpp - argument translation and execution for the MCP tool surface.
// Module: mcp (C++17).
// Owns: the per command argument table and the run of one captured command.
// Depends: re_mcp_tools.h, re_cmds. Commands get the same argv shape the CLI passes.
#include "mcp/re_mcp_tools.h"

#include "features/debug/re_dbg.h"
#include "features/sess/re_dbg_lock.h"
#include "utils/json/re_jr.h"
#include "cli/cmds/re_cmds.h"

#include <cstdio>
#include <cstring>

namespace {

void prop(re_strbuf_t *b, bool *first, const char *name, const char *kind, const char *desc) {
    if (!*first)
        re_strbuf_putc(b, ',');
    *first = false;
    re_strbuf_puts(b, "\"");
    re_strbuf_puts(b, name);
    re_strbuf_puts(b, "\":{\"type\":\"");
    re_strbuf_puts(b, kind);
    re_strbuf_puts(b, "\",\"description\":");
    re_jr_escape(b, re_str(desc));
    re_strbuf_putc(b, '}');
}

bool name_is(const re_cmd_t *cmd, const char *n) {
    return re_str_eq_cstr(re_str(cmd->name), n);
}

bool name_in(const re_cmd_t *cmd, const char *const *names) {
    for (size_t i = 0; names[i]; i++) {
        if (name_is(cmd, names[i]))
            return true;
    }
    return false;
}

char *dup_i64(re_arena_t *a, int64_t n) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", (long long)n);
    return (char *)re_arena_strdup(a, buf);
}

void push_str(re_arena_t *a, char **argv, int *argc, re_str_t s) {
    if (!s.n || *argc >= 15)
        return;
    argv[(*argc)++] = (char *)re_arena_strndup(a, s.p, s.n);
}

void push_cstr(re_arena_t *a, char **argv, int *argc, const char *s) {
    if (!s || !s[0] || *argc >= 15)
        return;
    argv[(*argc)++] = (char *)re_arena_strdup(a, s);
}

void push_flag_i64(re_arena_t *a, char **argv, int *argc, const char *flag, int64_t n) {
    if (n < 0 || *argc + 1 >= 15)
        return;
    argv[(*argc)++] = (char *)re_arena_strdup(a, flag);
    argv[(*argc)++] = dup_i64(a, n);
}

void push_flag_bool(re_arena_t *a, char **argv, int *argc, const char *flag, bool on) {
    if (!on || *argc >= 15)
        return;
    argv[(*argc)++] = (char *)re_arena_strdup(a, flag);
}

int fill_from_argv_array(re_arena_t *a, const re_jr_t *arr, char **argv) {
    int argc = 0;
    if (!arr || arr->kind != RE_JR_ARR || !arr->items)
        return 0;
    for (size_t i = 0; i < arr->count && argc < 15; i++) {
        re_str_t s = re_jr_str(&arr->items[i], "");
        push_str(a, argv, &argc, s);
    }
    return argc;
}

int fill_argv(re_arena_t *a, const re_jr_t *args, char **argv) {
    int argc = 0;
    if (!args)
        return 0;
    argc = fill_from_argv_array(a, re_jr_get(args, "argv"), argv);
    if (argc)
        goto flags;
    /* Named fields map to CLI positionals; empty values are skipped. */
    push_str(a, argv, &argc, re_jr_str(re_jr_get(args, "pid"), ""));
    push_str(a, argv, &argc, re_jr_str(re_jr_get(args, "tid"), ""));
    push_str(a, argv, &argc, re_jr_str(re_jr_get(args, "addr"), ""));
    push_str(a, argv, &argc, re_jr_str(re_jr_get(args, "expr"), ""));
    push_str(a, argv, &argc, re_jr_str(re_jr_get(args, "expression"), ""));
    push_str(a, argv, &argc, re_jr_str(re_jr_get(args, "name"), ""));
    push_str(a, argv, &argc, re_jr_str(re_jr_get(args, "pattern"), ""));
    push_str(a, argv, &argc, re_jr_str(re_jr_get(args, "hex"), ""));
    push_str(a, argv, &argc, re_jr_str(re_jr_get(args, "value"), ""));
    push_str(a, argv, &argc, re_jr_str(re_jr_get(args, "index"), ""));
    push_str(a, argv, &argc, re_jr_str(re_jr_get(args, "size"), ""));
    push_str(a, argv, &argc, re_jr_str(re_jr_get(args, "access"), ""));
    push_str(a, argv, &argc, re_jr_str(re_jr_get(args, "code"), ""));
    push_str(a, argv, &argc, re_jr_str(re_jr_get(args, "sub"), ""));
    push_str(a, argv, &argc, re_jr_str(re_jr_get(args, "asm"), ""));
    push_str(a, argv, &argc, re_jr_str(re_jr_get(args, "text"), ""));
    push_str(a, argv, &argc, re_jr_str(re_jr_get(args, "timeout_ms"), ""));
flags:
    push_flag_bool(a, argv, &argc, "--handle", re_jr_bool(re_jr_get(args, "handle"), false));
    push_flag_i64(a, argv, &argc, "--limit", re_jr_i64(re_jr_get(args, "limit"), -1));
    push_flag_i64(a, argv, &argc, "--offset", re_jr_i64(re_jr_get(args, "offset"), -1));
    push_flag_i64(a, argv, &argc, "--len", re_jr_i64(re_jr_get(args, "len"), -1));
    push_flag_i64(a, argv, &argc, "--off", re_jr_i64(re_jr_get(args, "off"), -1));
    {
        re_str_t fmt = re_jr_str(re_jr_get(args, "format"), "");
        if (fmt.n && argc + 1 < 15) {
            push_cstr(a, argv, &argc, "--format");
            push_str(a, argv, &argc, fmt);
        }
    }
    {
        re_str_t cursor = re_jr_str(re_jr_get(args, "cursor"), "");
        if (cursor.n && argc + 1 < 15) {
            push_cstr(a, argv, &argc, "--cursor");
            push_str(a, argv, &argc, cursor);
        }
    }
    return argc;
}

bool capture_out(FILE *tmp, re_arena_t *a, const re_cmd_t *cmd, re_strbuf_t *out, bool *is_error) {
    long sz = 0;
    if (std::fseek(tmp, 0, SEEK_END) != 0) {
        mcp_tool_error(out, RE_E_IO, "capture seek failed", is_error);
        return false;
    }
    sz = std::ftell(tmp);
    if (sz < 0) {
        mcp_tool_error(out, RE_E_IO, "capture tell failed", is_error);
        return false;
    }
    std::rewind(tmp);
    if (sz == 0) {
        re_strbuf_puts(out, "{\"schema\":\"antinomia/1\",\"tool\":\"");
        re_strbuf_puts(out, cmd->name);
        re_strbuf_puts(out, "\",\"ok\":true}");
        return true;
    }
    char *buf = (char *)re_arena_alloc(a, (size_t)sz + 1u);
    if (!buf || std::fread(buf, 1, (size_t)sz, tmp) != (size_t)sz) {
        mcp_tool_error(out, RE_E_IO, "capture read failed", is_error);
        return false;
    }
    buf[sz] = 0;
    /* Strip trailing newlines: MCP uses newline framing, so embedded LFs
     * would split the JSON-RPC frame. */
    while (sz > 0 && (buf[sz - 1] == '\n' || buf[sz - 1] == '\r'))
        buf[--sz] = 0;
    re_strbuf_append(out, buf, (size_t)sz);
    return true;
}

void schema_common(re_strbuf_t *b, bool *first) {
    prop(b, first, "format", "string", "json or text");
    prop(b, first, "limit", "integer", "Row cap for paging (default 200)");
    prop(b, first, "offset", "integer", "Skip the first N rows");
    prop(b, first, "argv", "array", "Positional argv strings (overrides named fields)");
}

void schema_props(const re_cmd_t *cmd, re_strbuf_t *b, bool *first) {
    static const char *kAddr[] = {
        "bp", "bc", "be",    "bd",  "bph",      "bphw",      "bphc", "bpm", "bpmc", "bpc", "bpfr",
        "d",  "u",  "graph", "cfg", "mem_read", "mem_write", "src",  "a",   "asm",  "bm",  NULL};
    static const char *kExpr[] = {"expr", "?", "watch", "wa", "bplogc", "bpcmdc", NULL};
    static const char *kText[] = {"note", "log", "bplog", "bpcmd", NULL};
    static const char *kTid[] = {"thread", "switch", "suspend", "resume", NULL};
    static const char *kName[] = {"bpdll", "bpdllc", "var", "sym", NULL};
    if (cmd->needs_path)
        prop(b, first, "path", "string", "Path to the target binary or script");
    if (name_is(cmd, "attach"))
        prop(b, first, "pid", "string", "Process id to attach");
    if (name_is(cmd, "wait_event"))
        prop(b, first, "timeout_ms", "string", "Wait timeout in milliseconds");
    if (name_in(cmd, kAddr) || name_is(cmd, "bplog") || name_is(cmd, "bpcmd") ||
        name_is(cmd, "bplogc") || name_is(cmd, "bpcmdc"))
        prop(b, first, "addr", "string", "Address or expression (hex/decimal/expr)");
    if (name_in(cmd, kExpr) || name_is(cmd, "bpc"))
        prop(b, first, "expr", "string", "C-like expression");
    if (name_in(cmd, kText))
        prop(b, first, "text", "string", "Text; {rax}/{$var} braces expand when applicable");
    if (name_in(cmd, kTid))
        prop(b, first, "tid", "string", "Thread id");
    if (name_in(cmd, kName))
        prop(b, first, "name", "string", "Module, symbol, or variable name");
    if (name_is(cmd, "find") || name_is(cmd, "search")) {
        prop(b, first, "pattern", "string", "Hex, quoted ASCII, or u32:value pattern");
        prop(b, first, "off", "integer", "Start address");
        prop(b, first, "len", "integer", "Span to scan (0 = modules)");
    }
    if (name_is(cmd, "mem_read") || name_is(cmd, "d") || name_is(cmd, "u") ||
        name_is(cmd, "mem.page") || name_is(cmd, "mem_page"))
        prop(b, first, "len", "integer", "Byte or instruction count");
    if (name_is(cmd, "mem_write"))
        prop(b, first, "hex", "string", "Hex bytes to write");
    if (name_is(cmd, "mem.page") || name_is(cmd, "mem_page"))
        prop(b, first, "addr", "string", "Target address (hex or decimal)");
    if (name_is(cmd, "bph") || name_is(cmd, "bphw") || name_is(cmd, "bpm")) {
        prop(b, first, "size", "string", "Watch size: 1, 2, 4, or 8");
        prop(b, first, "access", "string", "Access: x, w, or rw");
    }
    if (name_is(cmd, "a") || name_is(cmd, "asm"))
        prop(b, first, "asm", "string", "Assembly text to encode at addr");
    if (name_is(cmd, "exbp") || name_is(cmd, "exignore") || name_is(cmd, "expass") ||
        name_is(cmd, "exclear"))
        prop(b, first, "code", "string", "Exception or signal code (lo)");
    if (name_is(cmd, "exbp"))
        prop(b, first, "handle", "boolean", "Break and handle (do not pass)");
    if (name_is(cmd, "trace") || name_is(cmd, "tr") || name_is(cmd, "traces") ||
        name_is(cmd, "maps") || name_is(cmd, "mmap"))
        prop(b, first, "sub", "string", "Subcommand (start/stop/clear/follow/...)");
    if (name_is(cmd, "tracestep"))
        prop(b, first, "value", "string", "Step count while recording");
    if (name_is(cmd, "trace.query") || name_is(cmd, "replay.query") || name_is(cmd, "mem.page") ||
        name_is(cmd, "mem_page") || name_is(cmd, "maps") || name_is(cmd, "modules") ||
        name_is(cmd, "state"))
        prop(b, first, "cursor", "string", "Opaque page cursor (same as offset decimal)");
    if (name_is(cmd, "replay.frame")) {
        prop(b, first, "index", "string", "Absolute frame index from the Phase 2 trace file");
        prop(b, first, "value", "string", "Alias for index");
    }
    schema_common(b, first);
}

} // namespace

void mcp_tool_schema(const re_cmd_t *cmd, re_strbuf_t *b) {
    bool first = true;
    re_strbuf_puts(b, "{\"type\":\"object\",\"properties\":{");
    schema_props(cmd, b, &first);
    re_strbuf_puts(b, "}");
    if (cmd->needs_path)
        re_strbuf_puts(b, ",\"required\":[\"path\"]");
    else if (name_is(cmd, "attach"))
        re_strbuf_puts(b, ",\"required\":[\"pid\"]");
    re_strbuf_putc(b, '}');
}

void mcp_tool_error(re_strbuf_t *out, re_err_code_t code, const char *msg, bool *is_error) {
    re_strbuf_puts(out, "{\"schema\":\"antinomia/1\",\"ok\":false,\"error\":{\"code\":\"");
    re_strbuf_puts(out, re_err_json_code(code));
    re_strbuf_puts(out, "\",\"message\":");
    re_jr_escape(out, re_str(msg ? msg : re_err_str(code)));
    if (code == RE_E_PRIVILEGE) {
        re_err_privilege_t priv;
        re_err_privilege_fill("access", &priv);
        re_strbuf_puts(out, ",\"kind\":\"");
        re_strbuf_puts(out, priv.kind);
        re_strbuf_puts(out, "\",\"platform\":\"");
        re_strbuf_puts(out, priv.platform);
        re_strbuf_puts(out, "\",\"hint\":");
        re_jr_escape(out, re_str(priv.hint));
    }
    re_strbuf_puts(out, "}}");
    *is_error = true;
}

namespace {

struct ToolJob {
    const re_cmd_t *cmd;
    const char *path;
    const re_jr_t *args;
    re_arena_t *a;
    re_strbuf_t *out;
    bool *is_error;
};

void tool_job_run(void *ctx) {
    ToolJob *j = (ToolJob *)ctx;
    char *argv_store[16];
    int argc = 0;
    int first = 0;
    FILE *tmp = std::tmpfile();
    re_err_t err{};
    re_ctx_t c{};
    if (!tmp) {
        mcp_tool_error(j->out, RE_E_IO, "could not open temp capture", j->is_error);
        return;
    }
    argc = fill_argv(j->a, j->args, argv_store);
    err.code = RE_OK;
    c.arena = j->a;
    c.err = &err;
    c.stream = tmp;
    c.session = re_dbg_session_global();
    if (!re_cmd_parse(&c, argc, argv_store, &first, &err)) {
        std::fclose(tmp);
        mcp_tool_error(j->out, err.code != RE_OK ? err.code : RE_E_USAGE, re_err_msg(&err),
                       j->is_error);
        return;
    }
    (void)j->cmd->fn(&c, j->path, argc - first, argv_store + first);
    if (!capture_out(tmp, j->a, j->cmd, j->out, j->is_error)) {
        std::fclose(tmp);
        return;
    }
    std::fclose(tmp);
    if (err.code != RE_OK)
        *j->is_error = true;
}

} // namespace

void mcp_run_tool(const re_cmd_t *cmd, const char *path, const re_jr_t *args, re_arena_t *a,
                  re_strbuf_t *out, bool *is_error) {
    ToolJob job;
    job.cmd = cmd;
    job.path = path;
    job.args = args;
    job.a = a;
    job.out = out;
    job.is_error = is_error;
    /* Queue into the TUI owner thread when present; never block wait_event. */
    re_dbg_lock_submit(tool_job_run, &job);
}
