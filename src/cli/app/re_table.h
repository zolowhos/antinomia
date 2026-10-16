// re_table.h - the one command table that feeds both the CLI and the MCP tool list.
// Module: cli (C11).
// Owns: command metadata, shared argument parsing and the dispatch contract.
// Depends: re_str, re_strbuf, re_err. No globals except the table itself.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "utils/mem/re_arena.h"
#include "utils/sys/re_err.h"
#include "utils/text/re_str.h"
#include "utils/text/re_strbuf.h"

#define RE_CMD_NAME_MAX 48
#define RE_CMD_SUMMARY_MAX 96

typedef enum {
    RE_FMT_OUT_JSON = 0,
    RE_FMT_OUT_TEXT,
} re_out_fmt_t;

typedef struct re_ctx {
    re_arena_t *arena;
    re_out_fmt_t out;
    size_t limit;
    size_t offset;
    uint64_t off;
    uint64_t len;
    bool no_color;
    void *stream;
    // Borrowed long-lived debug session, or NULL for a one-shot command.
    struct re_dbg_session *session;
    re_err_t *err;
} re_ctx_t;

typedef int (*re_cmd_fn)(re_ctx_t *ctx, const char *path, int argc, char **argv);

typedef struct {
    char name[RE_CMD_NAME_MAX];
    char summary[RE_CMD_SUMMARY_MAX];
    char usage[64];
    re_cmd_fn fn;
    bool needs_path;
    bool interactive;
    bool mcp;
} re_cmd_t;

const re_cmd_t *re_cmd_table(size_t *count);
const re_cmd_t *re_cmd_find(const char *name);
void *re_ctx_out(const re_ctx_t *ctx);
bool re_cmd_parse(re_ctx_t *ctx, int argc, char **argv, int *first_positional, re_err_t *err);
const char *re_cmd_positional(int argc, char **argv, int from);
void re_cmd_begin(re_ctx_t *ctx, re_strbuf_t *head);

#ifdef __cplusplus
}
#endif
