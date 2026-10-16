// re_gui.c - empty debugger welcome: CPU-like panes with no live inferior yet.
// Module: cli (C11).
// Owns: the Phase 0 welcome loop and the empty pane layout.
// Depends: re_gui.h, term, input, focus, draw, screen. No live debug.
#include "cli/screen/re_gui.h"

#include "utils/tui/re_screen.h"
#include "cli/screen/re_draw.h"
#include "cli/screen/re_focus.h"
#include "cli/screen/re_input.h"
#include "cli/screen/re_term.h"

#include <stdio.h>
#include <string.h>

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

static void paint_welcome(re_screen_t *s, const char *path) {
    uint16_t mid;
    re_screen_clear(s);
    re_screen_put_run(s, 0, 1, s->cols > 2 ? (uint16_t)(s->cols - 2) : 0,
                      "antinomia 0.1.0 - AI driven debugger (Phase 0 stub)", (uint8_t)RE_ST_TITLE,
                      0);
    re_screen_put_run(s, 1, 1, s->cols > 2 ? (uint16_t)(s->cols - 2) : 0,
                      "panes: disasm | regs | dump | stack (empty until Phase 1)",
                      (uint8_t)RE_ST_LABEL, 0);
    if (path && path[0])
        re_screen_put_run(s, 2, 1, s->cols > 2 ? (uint16_t)(s->cols - 2) : 0, path,
                          (uint8_t)RE_ST_NONE, 0);
    else
        re_screen_put_run(s, 2, 1, s->cols > 2 ? (uint16_t)(s->cols - 2) : 0,
                          "no target - press q to leave", (uint8_t)RE_ST_NONE, 0);
    if (s->rows < 12 || s->cols < 40)
        return;
    mid = (uint16_t)(s->cols / 2u);
    re_screen_box(s, 4, 0, mid, 5, "disasm", (uint8_t)RE_ST_NONE);
    re_screen_box(s, 4, mid, (uint16_t)(s->cols - mid), 5, "regs", (uint8_t)RE_ST_NONE);
    re_screen_box(s, 9, 0, mid, 5, "dump", (uint8_t)RE_ST_NONE);
    re_screen_box(s, 9, mid, (uint16_t)(s->cols - mid), 5, "stack", (uint8_t)RE_ST_NONE);
    re_screen_put_run(s, 5, 2, mid > 4 ? (uint16_t)(mid - 4) : 0, "(empty)", (uint8_t)RE_ST_LABEL,
                      0);
    re_screen_put_run(s, 5, (uint16_t)(mid + 2), 8, "(empty)", (uint8_t)RE_ST_LABEL, 0);
    re_screen_put_run(s, 10, 2, mid > 4 ? (uint16_t)(mid - 4) : 0, "(empty)", (uint8_t)RE_ST_LABEL,
                      0);
    re_screen_put_run(s, 10, (uint16_t)(mid + 2), 8, "(empty)", (uint8_t)RE_ST_LABEL, 0);
    re_screen_status(s, (uint16_t)(s->rows - 1), 0, s->cols, "q quit", "phase0");
}

int re_cmd_gui(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    re_term_t term;
    re_arena_t arena;
    re_draw_t draw;
    re_screen_t frame;
    re_focus_t focus;
    uint16_t rows = 24, cols = 80;
    bool quit = false;
    (void)argc;
    (void)argv;
    if (!re_term_open(&term, false)) {
        if (ctx && ctx->err)
            RE_ERR_SET(ctx->err, RE_E_USAGE, "gui needs a terminal on stdin and stdout");
        fprintf(stderr, "gui needs a terminal on both ends; try status or mcp\n");
        return 2;
    }
    re_arena_init(&arena, 256u * 1024u);
    re_term_size(&rows, &cols);
    if (!re_draw_init(&draw, &arena, rows, cols) ||
        !re_screen_init(&frame, &arena, &draw.out, rows, cols)) {
        re_term_close(&term);
        re_arena_free(&arena);
        return 2;
    }
    re_focus_init(&focus);
    while (!quit) {
        re_ev_t ev;
        paint_welcome(&frame, path);
        re_focus_build(&focus, &frame);
        if (!draw.valid)
            re_draw_full(&draw, &frame);
        else
            re_draw_frame(&draw, &frame);
        if (draw.out.len) {
            re_draw_home(&draw, 0, 0);
            re_term_write(&term, draw.out.p, draw.out.len);
        }
        if (!re_term_wait(&term, &ev, 250u)) {
            uint16_t nr = 0, nc = 0;
            re_term_size(&nr, &nc);
            if (nr != frame.rows || nc != frame.cols) {
                re_draw_resize(&draw, &arena, nr, nc);
                re_screen_init(&frame, &arena, &draw.out, nr, nc);
            }
            continue;
        }
        if (ev.kind == RE_EV_RESIZE) {
            uint16_t nr = 0, nc = 0;
            re_term_size(&nr, &nc);
            re_draw_resize(&draw, &arena, nr, nc);
            re_screen_init(&frame, &arena, &draw.out, nr, nc);
            continue;
        }
        if (ev.kind == RE_EV_KEY && (ev.key == 'q' || ev.key == 'Q' || ev.key == RE_KEY_ESCAPE))
            quit = true;
    }
    re_term_close(&term);
    re_arena_free(&arena);
    return 0;
}
