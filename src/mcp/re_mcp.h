// re_mcp.h - entry point for the MCP JSON-RPC server.
// Module: mcp (C++17).
// Owns: the start of the server loop over stdio or caller-owned streams.
// Depends: re_utils. main() in src/cli/app/main.cpp owns the dispatch into this.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif
const char *re_mcp_protocol_version(void);
int re_mcp_main(void);
int re_mcp_serve_files(FILE *in, FILE *out);
typedef size_t (*re_mcp_read_fn)(void *ctx, char *buf, size_t cap);
typedef bool (*re_mcp_write_fn)(void *ctx, const char *data, size_t n);
int re_mcp_serve_callbacks(void *rctx, re_mcp_read_fn rf, void *wctx, re_mcp_write_fn wf);
#ifdef __cplusplus
}
#endif
