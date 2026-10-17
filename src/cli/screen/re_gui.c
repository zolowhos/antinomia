// re_gui.c - full-screen CPU view bound to the global debug session.
// Module: cli (C11).
// Owns: the interactive TUI loop, focus cycling, and frame refresh.
// Depends: re_gui.h, re_cpu, term, input, focus, draw, screen, re_dbg.
#include "cli/screen/re_gui.h"

#include "features/debug/re_dbg.h"
#include "features/sess/re_dbg_lock.h"
#include "utils/tui/re_screen.h"
#include "cli/screen/re_cpu.h"
#include "cli/screen/re_draw.h"
#include "cli/screen/re_focus.h"
#include "cli/screen/re_input.h"
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

static bool handle_key(re_cpu_view_t *cpu, re_focus_t *focus, const re_ev_t *ev, bool *quit) {
    if (ev->kind != RE_EV_KEY)
        return false;
    if (ev->key == 'q' || ev->key == 'Q' || ev->key == RE_KEY_ESCAPE) {
        *quit = true;
        return true;
    }
    if (ev->key == RE_KEY_TAB) {
        re_cpu_focus_next(cpu);
        if (cpu->zones[cpu->focus])
            re_focus_set(focus, cpu->zones[cpu->focus]);
        return true;
    }
    if (ev->key == RE_KEY_STAB) {
        re_cpu_focus_prev(cpu);
        if (cpu->zones[cpu->focus])
            re_focus_set(focus, cpu->zones[cpu->focus]);
        return true;
    }
    return false;
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
                     re_cpu_view_t *cpu, re_dbg_session_t *session, re_term_t *term) {
    bool quit = false;
    while (!quit) {
        re_ev_t ev;
        paint_frame(draw, frame, focus, cpu, session, term);
        if (!re_term_wait(term, &ev, 250u)) {
            uint16_t nr = 0, nc = 0;
            re_term_size(&nr, &nc);
            if (nr != frame->rows || nc != frame->cols) {
                resize_frame(arena, draw, frame);
                re_cpu_mark_dirty_all(cpu);
            }
            continue;
        }
        if (ev.kind == RE_EV_RESIZE) {
            resize_frame(arena, draw, frame);
            re_cpu_mark_dirty_all(cpu);
            continue;
        }
        if (handle_key(cpu, focus, &ev, &quit))
            continue;
        handle_click(cpu, focus, frame, &ev);
    }
}

int re_cmd_gui(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_term_t term;
    re_arena_t arena;
    re_draw_t draw;
    re_screen_t frame;
    re_focus_t focus;
    re_cpu_view_t cpu;
    re_dbg_session_t *session;
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
    note_mcp_endpoint(&cpu);
    re_dbg_lock_set_owner(true);
    gui_loop(&arena, &draw, &frame, &focus, &cpu, session, &term);
    re_dbg_lock_set_owner(false);
    re_mcp_side_stop();
    re_term_close(&term);
    re_arena_free(&arena);
    return 0;
}
