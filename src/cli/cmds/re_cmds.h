// re_cmds.h - Phase 0 debugger command entry points.
// Module: cli (C11).
// Owns: declarations for stub debug commands and status.
// Depends: re_table.h. Live backends arrive in Phase 1.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "cli/app/re_table.h"

int re_cmd_status(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_launch(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_attach(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_detach(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_wait_event(re_ctx_t *ctx, const char *path, int argc, char **argv);
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
