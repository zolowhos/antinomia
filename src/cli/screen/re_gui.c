// re_gui.c - full-screen CPU view bound to the global debug session.
// Module: cli (C11).
// Owns: the interactive TUI loop, focus cycling, and frame refresh.
// Depends: re_gui.h, re_cpu, term, input, focus, draw, keys, theme, screen, re_dbg.
#include "cli/screen/re_gui.h"

#include "features/debug/re_dbg.h"
#include "features/sess/re_dbg_lock.h"
#include "utils/cfg/re_theme.h"
#include "utils/tui/re_screen.h"
#include "cli/screen/re_cpu.h"
#include "cli/screen/re_draw.h"
#include "cli/screen/re_focus.h"
#include "cli/screen/re_input.h"
#include "cli/screen/re_keys.h"
#include "cli/screen/re_term.h"
#include "mcp/re_mcp_side.h"

#include <stdio.h>

#if defined(_WIN32)
#    include <io.h>
#else
#    include <unistd.h>
#endif

bool re_gui_wants_file(const char *arg) {
    if (!arg || !arg[0])
        return false;
#if defined(_WIN32)
    return _access(arg, 0) == 0;
#else
    return access(arg, R_OK) == 0;
#endif
}

static re_err_code_t toggle_bp_at(re_dbg_session_t *session, uint64_t addr, const char **msg) {
    re_dbg_bp_info_t list[32];
    size_t n = 0, i;
    bool found = false;
    if (!addr)
        return RE_E_USAGE;
    (void)re_dbg_session_bp_list(session, list, 32, &n);
    for (i = 0; i < n; i++) {
        if (list[i].addr == addr) {
            found = true;
            break;
        }
    }
    *msg = found ? "bc" : "bp";
    return found ? re_dbg_session_bp_clear(session, addr) : re_dbg_session_bp_set(session, addr);
}

static void session_act(re_cpu_view_t *cpu, re_dbg_session_t *session, re_act_t act) {
    re_err_code_t c = RE_OK;
    const char *msg = re_keys_act_name(act);
    uint64_t addr = cpu->rip ? cpu->rip : session->last_event.addr;
    re_dbg_lock_acquire();
    if (act == RE_ACT_RUN)
        c = re_dbg_session_cont(session);
    else if (act == RE_ACT_STEP_INTO)
        c = re_dbg_session_step(session);
    else if (act == RE_ACT_STEP_OVER)
        c = re_dbg_session_step_over(session);
    else if (act == RE_ACT_PAUSE)
        c = re_dbg_session_pause(session);
    else if (act == RE_ACT_TOGGLE_BP)
        c = toggle_bp_at(session, addr, &msg);
    re_dbg_lock_release();
    if (c == RE_OK)
        re_cpu_log_push(cpu, msg);
    else
        re_cpu_log_push(cpu, "key: unsupported");
}

static bool handle_key(re_cpu_view_t *cpu, re_focus_t *focus, re_dbg_session_t *session,
                       const re_keys_t *keys, const re_ev_t *ev, bool *quit) {
    re_act_t act;
    if (ev->kind != RE_EV_KEY)
        return false;
    act = re_keys_match(keys, ev);
    if (act == RE_ACT_NONE)
        return false;
    if (act == RE_ACT_QUIT) {
        *quit = true;
        return true;
    }
    if (act == RE_ACT_FOCUS_NEXT) {
        re_cpu_focus_next(cpu);
        if (cpu->zones[cpu->focus])
            re_focus_set(focus, cpu->zones[cpu->focus]);
        return true;
    }
    if (act == RE_ACT_FOCUS_PREV) {
        re_cpu_focus_prev(cpu);
        if (cpu->zones[cpu->focus])
            re_focus_set(focus, cpu->zones[cpu->focus]);
        return true;
    }
    session_act(cpu, session, act);
    return true;
}

static void handle_click(re_cpu_view_t *cpu, re_focus_t *focus, const re_screen_t *frame,
                         const re_ev_t *ev) {
    if (!re_input_is_click(ev))
        return;
    if (!re_focus_click(focus, frame, ev->row, ev->col))
        return;
    (void)re_cpu_focus_zone(cpu, focus->zone);
}

static bool ensure_frame(re_arena_t *arena, re_draw_t *draw, re_screen_t *frame, uint16_t rows,
                         uint16_t cols) {
    if (!re_draw_init(draw, arena, rows, cols))
        return false;
    return re_screen_init(frame, arena, &draw->out, rows, cols);
}

static void paint_frame(re_draw_t *draw, re_screen_t *frame, re_focus_t *focus, re_cpu_view_t *cpu,
                        re_dbg_session_t *session, re_term_t *term) {
    re_dbg_lock_drain();
    re_dbg_lock_acquire();
    re_cpu_refresh(cpu, session);
    re_dbg_lock_release();
    re_cpu_paint(frame, cpu);
    re_focus_build(focus, frame);
    if (cpu->zones[cpu->focus])
        re_focus_set(focus, cpu->zones[cpu->focus]);
    if (!draw->valid)
        re_draw_full(draw, frame);
    else
        re_draw_frame(draw, frame);
    if (draw->out.len) {
        re_draw_home(draw, 0, 0);
        re_term_write(term, draw->out.p, draw->out.len);
    }
}

static void resize_frame(re_arena_t *arena, re_draw_t *draw, re_screen_t *frame) {
    uint16_t nr = 0, nc = 0;
    re_term_size(&nr, &nc);
    re_draw_resize(draw, arena, nr, nc);
    re_screen_init(frame, arena, &draw->out, nr, nc);
}

static void seed_session_path(re_dbg_session_t *session, const char *path) {
    size_t i = 0;
    if (!path || !path[0] || session->path[0])
        return;
    while (path[i] && i + 1u < sizeof(session->path)) {
        session->path[i] = path[i];
        i++;
    }
    session->path[i] = 0;
}

static void note_mcp_endpoint(re_cpu_view_t *cpu) {
    char ep[256];
    char line[288];
    size_t n = 0;
    const char *p;
    if (!re_mcp_side_start(ep, sizeof(ep)))
        return;
    p = "mcp ";
    while (*p && n + 1u < sizeof(line))
        line[n++] = *p++;
    p = ep;
    while (*p && n + 1u < sizeof(line))
        line[n++] = *p++;
    line[n] = 0;
    re_cpu_log_push(cpu, line);
}

static void gui_loop(re_arena_t *arena, re_draw_t *draw, re_screen_t *frame, re_focus_t *focus,
                     re_cpu_view_t *cpu, re_dbg_session_t *session, re_term_t *term,
                     const re_keys_t *keys) {
    bool quit = false;
    while (!quit) {
        re_ev_t ev;
        paint_frame(draw, frame, focus, cpu, session, term);
        if (!re_term_wait(term, &ev, 250u)) {
            uint16_t nr = 0, nc = 0;
            re_term_size(&nr, &nc);
            if (nr != frame->rows || nc != frame->cols)
                resize_frame(arena, draw, frame);
            continue;
        }
        if (ev.kind == RE_EV_RESIZE) {
            resize_frame(arena, draw, frame);
            continue;
        }
        if (handle_key(cpu, focus, session, keys, &ev, &quit))
            continue;
        handle_click(cpu, focus, frame, &ev);
    }
}

static void load_ux(re_theme_t *theme, re_keys_t *keys, re_cpu_view_t *cpu) {
    if (re_theme_autoload(theme))
        re_cpu_log_push(cpu, "theme loaded");
    else
        re_cpu_log_push(cpu, "theme default");
    if (re_keys_autoload(keys))
        re_cpu_log_push(cpu, "keys loaded");
    else
        re_cpu_log_push(cpu, "keys x64dbg-ish");
    re_cpu_log_push(cpu, "F9 run F7/F8 step F2 bp F12 pause");
}

int re_cmd_gui(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_term_t term;
    re_arena_t arena;
    re_draw_t draw;
    re_screen_t frame;
    re_focus_t focus;
    re_cpu_view_t cpu;
    re_dbg_session_t *session;
    re_theme_t theme;
    re_keys_t keys;
    uint16_t rows = 24, cols = 80;
    (void)argc;
    (void)argv;
    if (!re_term_open(&term, false)) {
        if (ctx && ctx->err)
            RE_ERR_SET(ctx->err, RE_E_USAGE, "gui needs a terminal on stdin and stdout");
        fprintf(stderr, "gui needs a terminal on both ends; try status or mcp\n");
        return 2;
    }
    session = ctx && ctx->session ? ctx->session : re_dbg_session_global();
    seed_session_path(session, path);
    re_arena_init(&arena, 256u * 1024u);
    re_term_size(&rows, &cols);
    if (!ensure_frame(&arena, &draw, &frame, rows, cols)) {
        re_term_close(&term);
        re_arena_free(&arena);
        return 2;
    }
    re_focus_init(&focus);
    re_cpu_init(&cpu);
    load_ux(&theme, &keys, &cpu);
    note_mcp_endpoint(&cpu);
    re_dbg_lock_set_owner(true);
    gui_loop(&arena, &draw, &frame, &focus, &cpu, session, &term, &keys);
    re_dbg_lock_set_owner(false);
    re_mcp_side_stop();
    re_tui_styles_reset();
    re_term_close(&term);
    re_arena_free(&arena);
    return 0;
}
