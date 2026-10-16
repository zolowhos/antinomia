// re_mcp_tools.cpp - argument translation and execution for the MCP tool surface.
// Module: mcp (C++17).
// Owns: the per command argument table and the run of one captured command.
// Depends: re_mcp_tools.h, re_cmds. Commands get the same argv shape the CLI passes.
#include "mcp/re_mcp_tools.h"

#include "features/debug/re_dbg.h"
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

char *dup_i64(re_arena_t *a, int64_t n) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", (long long)n);
    return (char *)re_arena_strdup(a, buf);
}

void push_flag_i64(re_arena_t *a, char **argv, int *argc, const char *flag, int64_t n) {
    if (n < 0 || *argc + 1 >= 15)
        return;
    argv[(*argc)++] = (char *)re_arena_strdup(a, flag);
    argv[(*argc)++] = dup_i64(a, n);
}

int fill_argv(re_arena_t *a, const re_jr_t *args, char **argv) {
    int argc = 0;
    if (!args)
        return 0;
    re_str_t pid = re_jr_str(re_jr_get(args, "pid"), "");
    re_str_t addr = re_jr_str(re_jr_get(args, "addr"), "");
    re_str_t pattern = re_jr_str(re_jr_get(args, "pattern"), "");
    re_str_t text = re_jr_str(re_jr_get(args, "text"), "");
    if (pid.n && argc < 15)
        argv[argc++] = (char *)re_arena_strndup(a, pid.p, pid.n);
    if (addr.n && argc < 15)
        argv[argc++] = (char *)re_arena_strndup(a, addr.p, addr.n);
    if (pattern.n && argc < 15)
        argv[argc++] = (char *)re_arena_strndup(a, pattern.p, pattern.n);
    if (text.n && argc < 15)
        argv[argc++] = (char *)re_arena_strndup(a, text.p, text.n);
    push_flag_i64(a, argv, &argc, "--limit", re_jr_i64(re_jr_get(args, "limit"), -1));
    push_flag_i64(a, argv, &argc, "--offset", re_jr_i64(re_jr_get(args, "offset"), -1));
    push_flag_i64(a, argv, &argc, "--len", re_jr_i64(re_jr_get(args, "len"), -1));
    push_flag_i64(a, argv, &argc, "--off", re_jr_i64(re_jr_get(args, "off"), -1));
    return argc;
}

bool capture_out(FILE *tmp, re_arena_t *a, const re_cmd_t *cmd, re_strbuf_t *out, bool *is_error) {
    long sz = 0;
    if (std::fseek(tmp, 0, SEEK_END) != 0) {
        mcp_tool_error(out, "capture seek failed", is_error);
        return false;
    }
    sz = std::ftell(tmp);
    if (sz < 0) {
        mcp_tool_error(out, "capture tell failed", is_error);
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
        mcp_tool_error(out, "capture read failed", is_error);
        return false;
    }
    buf[sz] = 0;
    re_strbuf_append(out, buf, (size_t)sz);
    return true;
}

} // namespace

void mcp_tool_schema(const re_cmd_t *cmd, re_strbuf_t *b) {
    bool first = true;
    re_strbuf_puts(b, "{\"type\":\"object\",\"properties\":{");
    if (cmd->needs_path)
        prop(b, &first, "path", "string", "Path to the target binary");
    if (re_str_eq_cstr(re_str(cmd->name), "attach"))
        prop(b, &first, "pid", "string", "Process id to attach");
    if (re_str_eq_cstr(re_str(cmd->name), "mem_read") ||
        re_str_eq_cstr(re_str(cmd->name), "mem_write") ||
        re_str_eq_cstr(re_str(cmd->name), "graph") || re_str_eq_cstr(re_str(cmd->name), "cfg") ||
        re_str_eq_cstr(re_str(cmd->name), "u") || re_str_eq_cstr(re_str(cmd->name), "d")) {
        prop(b, &first, "addr", "string", "Target address (hex or decimal)");
        prop(b, &first, "len", "integer", "Byte count");
    }
    if (re_str_eq_cstr(re_str(cmd->name), "find") || re_str_eq_cstr(re_str(cmd->name), "search")) {
        prop(b, &first, "pattern", "string", "Hex, quoted ASCII, or u32:value pattern");
        prop(b, &first, "off", "integer", "Start address");
        prop(b, &first, "len", "integer", "Span to scan (0 = modules)");
    }
    prop(b, &first, "format", "string", "json or text");
    prop(b, &first, "limit", "integer", "Row cap for paging (default 200)");
    prop(b, &first, "offset", "integer", "Skip the first N rows");
    if (re_str_eq_cstr(re_str(cmd->name), "note") || re_str_eq_cstr(re_str(cmd->name), "log") ||
        re_str_eq_cstr(re_str(cmd->name), "bplog") || re_str_eq_cstr(re_str(cmd->name), "bpcmd")) {
        prop(b, &first, "text", "string", "Text; {rax}/{$var} braces expand when applicable");
    }
    re_strbuf_puts(b, "}");
    if (cmd->needs_path)
        re_strbuf_puts(b, ",\"required\":[\"path\"]");
    re_strbuf_putc(b, '}');
}

void mcp_tool_error(re_strbuf_t *out, const char *msg, bool *is_error) {
    re_strbuf_puts(out, "{\"schema\":\"antinomia/1\",\"error\":{\"code\":\"usage\",\"message\":");
    re_jr_escape(out, re_str(msg));
    re_strbuf_puts(out, "}}");
    *is_error = true;
}

void mcp_run_tool(const re_cmd_t *cmd, const char *path, const re_jr_t *args, re_arena_t *a,
                  re_strbuf_t *out, bool *is_error) {
    char *argv_store[16];
    int argc = 0;
    int first = 0;
    FILE *tmp = std::tmpfile();
    re_err_t err{};
    re_ctx_t ctx{};
    if (!tmp) {
        mcp_tool_error(out, "could not open temp capture", is_error);
        return;
    }
    argc = fill_argv(a, args, argv_store);
    err.code = RE_OK;
    ctx.arena = a;
    ctx.err = &err;
    ctx.stream = tmp;
    ctx.session = re_dbg_session_global();
    if (!re_cmd_parse(&ctx, argc, argv_store, &first, &err)) {
        std::fclose(tmp);
        mcp_tool_error(out, re_err_msg(&err), is_error);
        return;
    }
    (void)cmd->fn(&ctx, path, argc - first, argv_store + first);
    if (!capture_out(tmp, a, cmd, out, is_error)) {
        std::fclose(tmp);
        return;
    }
    std::fclose(tmp);
    if (err.code != RE_OK)
        *is_error = true;
}
