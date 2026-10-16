// re_cmds.h - debugger command entry points for CLI and MCP.
// Module: cli (C11).
// Owns: declarations for session-wired debug commands and status.
// Depends: re_table.h. Live backends arrive via the session vtable.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "cli/app/re_table.h"

int re_cmd_status(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_init(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_launch(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_attach(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_detach(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_kill(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_wait_event(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_run(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_cont(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_step(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_regs(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_set_regs(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_mem_read(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_mem_write(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_threads(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_modules(re_ctx_t *ctx, const char *path, int argc, char **argv);

#ifdef __cplusplus
}
#endif
