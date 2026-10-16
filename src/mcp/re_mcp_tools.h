// re_mcp_tools.h - the tools/call half of the MCP server.
// Module: mcp (C++17).
// Owns: per tool argument translation, schema generation and command execution.
// Depends: re_table, re_jr, re_strbuf. Runs commands; never touches stdio.
#pragma once

#include "utils/json/re_jr.h"
#include "utils/sys/re_err.h"
#include "utils/text/re_strbuf.h"
#include "cli/app/re_table.h"

void mcp_tool_schema(const re_cmd_t *cmd, re_strbuf_t *b);
void mcp_tool_error(re_strbuf_t *out, re_err_code_t code, const char *msg, bool *is_error);
void mcp_run_tool(const re_cmd_t *cmd, const char *path, const re_jr_t *args, re_arena_t *a,
                  re_strbuf_t *out, bool *is_error);
