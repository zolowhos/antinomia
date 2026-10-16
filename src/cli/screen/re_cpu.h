// re_cpu.h - CPU-like debugger panes bound to session state.
// Module: cli (C11).
// Owns: disasm/regs/dump/stack/bp/log view model and screen compose.
// Depends: re_screen, re_dbg. Degrades when backend ops are unsupported.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "features/debug/re_dbg.h"
#include "utils/tui/re_screen.h"

#define RE_CPU_LINE_CAP 96u
#define RE_CPU_DISASM_MAX 48u
#define RE_CPU_REGS_MAX 20u
#define RE_CPU_DUMP_MAX 24u
#define RE_CPU_STACK_MAX 24u
#define RE_CPU_BP_MAX 32u
#define RE_CPU_LOG_MAX 32u

typedef enum {
    RE_CPU_PANE_DISASM = 0,
    RE_CPU_PANE_REGS,
    RE_CPU_PANE_DUMP,
    RE_CPU_PANE_STACK,
    RE_CPU_PANE_BP,
    RE_CPU_PANE_LOG,
    RE_CPU_PANE_COUNT
} re_cpu_pane_t;

typedef struct {
    char text[RE_CPU_LINE_CAP];
} re_cpu_line_t;

typedef struct {
    uint64_t addr;
    bool enabled;
    char note[48];
} re_cpu_bp_t;

typedef struct {
    re_cpu_pane_t focus;
    uint8_t zones[RE_CPU_PANE_COUNT];

    bool attached;
    bool have_regs;
    bool have_mem;
    uint32_t pid;
    uint32_t tid;
    uint64_t rip;
    uint64_t rsp;
    uint64_t dump_addr;
    re_dbg_regs_t regs;
    re_dbg_event_kind_t last_kind;
    uint64_t last_addr;

    re_cpu_line_t disasm[RE_CPU_DISASM_MAX];
    size_t n_disasm;
    size_t rip_row;

    re_cpu_line_t reg_lines[RE_CPU_REGS_MAX];
    size_t n_regs;

    re_cpu_line_t dump[RE_CPU_DUMP_MAX];
    size_t n_dump;

    re_cpu_line_t stack[RE_CPU_STACK_MAX];
    size_t n_stack;

    re_cpu_bp_t bps[RE_CPU_BP_MAX];
    size_t n_bps;
    re_cpu_line_t bp_lines[RE_CPU_BP_MAX];
    size_t n_bp_lines;

    re_cpu_line_t log[RE_CPU_LOG_MAX];
    size_t n_log;
    size_t log_head;
    re_dbg_event_kind_t logged_kind;
    uint64_t logged_addr;

    char status_left[160];
    char status_right[64];
    const char *path;
} re_cpu_view_t;

void re_cpu_init(re_cpu_view_t *v);
void re_cpu_set_bps(re_cpu_view_t *v, const re_cpu_bp_t *bps, size_t n);
void re_cpu_log_push(re_cpu_view_t *v, const char *msg);
void re_cpu_refresh(re_cpu_view_t *v, re_dbg_session_t *s);
void re_cpu_focus_next(re_cpu_view_t *v);
void re_cpu_focus_prev(re_cpu_view_t *v);
bool re_cpu_focus_zone(re_cpu_view_t *v, uint8_t zone);

// Compose the CPU panes onto a cleared screen. Zones are assigned in pane order
// so Tab/focus walking matches disasm -> regs -> dump -> stack -> bp -> log.
void re_cpu_paint(re_screen_t *s, re_cpu_view_t *v);

#ifdef __cplusplus
}
#endif
