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
int re_cmd_step_into(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_step_over(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_pause(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_bp(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_bc(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_be(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_bd(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_bplist(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_bph(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_bph_clear(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_bpm(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_bpm_clear(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_bpdll(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_bpdll_clear(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_regs(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_set_regs(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_dump(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_disasm(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_mem_read(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_mem_write(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_threads(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_thread(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_suspend(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_resume(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_modules(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_maps(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_sym(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_bt(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_exbp(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_exignore(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_expass(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_exclear(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_exlist(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_caps(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_expr(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_var(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_watch(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_watchc(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_watchlist(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_bpc(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_bplog(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_bplogc(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_bpcmd(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_bpcmdc(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_bpfr(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_graph(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_mem_find(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_search(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_src(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_bm(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_bmlist(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_patches(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_funcs(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_asm(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_patchlist(re_ctx_t *ctx, const char *path, int argc, char **argv);
int re_cmd_patchwrite(re_ctx_t *ctx, const char *path, int argc, char **argv);

#ifdef __cplusplus
}
#endif
