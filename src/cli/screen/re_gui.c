// re_gui.c - full-screen CPU view with antistrefo-style chrome.
// Module: cli (C11).
// Owns: the interactive TUI loop, menu/button chrome, and frame refresh.
// Depends: re_gui.h, re_gui_menu, re_cpu, term, input, focus, draw, keys, theme.
#include "cli/screen/re_gui.h"

#include "cli/screen/keys/re_keys.h"
#include "cli/screen/menu/re_gui_menu.h"
#include "cli/screen/re_cpu.h"
#include "cli/screen/re_draw.h"
#include "cli/screen/re_focus.h"
#include "cli/screen/re_input.h"
#include "cli/screen/re_term.h"
#include "features/debug/re_dbg.h"
#include "features/sess/re_dbg_lock.h"
#include "mcp/re_mcp_side.h"
#include "utils/cfg/re_theme.h"
#include "utils/tui/re_screen.h"
#include "utils/tui/re_tui.h"
#include "utils/tui/re_ui.h"

#include <stdio.h>

#if defined(_WIN32)
#    include <io.h>
#else
#    include <unistd.h>
#endif

typedef struct {
    re_cpu_view_t *cpu;
    re_dbg_session_t *session;
    re_focus_t *focus;
    re_ui_t ui;
    re_keys_t keys;
    bool quit;
    bool color;
    bool repaint;
    int menu_open;
    uint8_t menu_zone;
    uint8_t menu_n;
    uint8_t btn_zone;
    uint8_t btn_n;
    re_act_t pending;
    size_t focus_idx;
} gui_loop_t;

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

static void log_cb(void *user, const char *msg) {
    if (user && msg)
        re_cpu_log_push((re_cpu_view_t *)user, msg);
}

static void menu_ctx_fill(gui_loop_t *g, re_gui_menu_ctx_t *mc) {
    mc->focus = &g->focus_idx;
    mc->n_panes = (size_t)RE_CPU_PANE_COUNT;
    mc->repaint = &g->repaint;
    mc->quit = &g->quit;
    mc->color = &g->color;
    mc->open = &g->menu_open;
    mc->bar = RE_SCREEN_ZONE_NONE;
    mc->zone = &g->menu_zone;
    mc->nitems = &g->menu_n;
    mc->pending = &g->pending;
    mc->log_fn = log_cb;
    mc->log_user = g->cpu;
    mc->path = g->session && g->session->path[0] ? g->session->path : "antinomia";
    mc->mark = "antinomia 0.1.0";
    mc->btn_zone = &g->btn_zone;
    mc->btn_n = &g->btn_n;
}

static bool handle_key(gui_loop_t *g, const re_ev_t *ev) {
    re_act_t act;
    if (ev->kind != RE_EV_KEY)
        return false;
    act = re_keys_match(&g->keys, ev);
    if (act == RE_ACT_NONE)
        return false;
    if (act == RE_ACT_QUIT) {
        g->quit = true;
        return true;
    }
    if (act == RE_ACT_FOCUS_NEXT) {
        re_cpu_focus_next(g->cpu);
        g->focus_idx = (size_t)g->cpu->focus;
        if (g->cpu->zones[g->cpu->focus])
            re_focus_set(g->focus, g->cpu->zones[g->cpu->focus]);
        return true;
    }
    if (act == RE_ACT_FOCUS_PREV) {
        re_cpu_focus_prev(g->cpu);
        g->focus_idx = (size_t)g->cpu->focus;
        if (g->cpu->zones[g->cpu->focus])
            re_focus_set(g->focus, g->cpu->zones[g->cpu->focus]);
        return true;
    }
    session_act(g->cpu, g->session, act);
    return true;
}

static void handle_click(gui_loop_t *g, const re_screen_t *frame, const re_ev_t *ev) {
    if (!re_input_is_click(ev))
        return;
    if (!re_focus_click(g->focus, frame, ev->row, ev->col))
        return;
    if (re_cpu_focus_zone(g->cpu, g->focus->zone))
        g->focus_idx = (size_t)g->cpu->focus;
}

static void feed_pointer(gui_loop_t *g, re_screen_t *frame, const re_ev_t *ev) {
    if (ev->button == RE_MOUSE_WHEEL_UP || ev->button == RE_MOUSE_WHEEL_DOWN)
        return;
    if ((ev->button & 64u) != 0)
        return;
    if ((ev->button & 32u) != 0) {
        re_ui_pointer(&g->ui, frame, ev->row, ev->col, RE_UI_MOVE);
        return;
    }
    if (!ev->press) {
        re_ui_pointer(&g->ui, frame, ev->row, ev->col, RE_UI_UP);
        return;
    }
    if (!re_input_is_click(ev))
        return;
    handle_click(g, frame, ev);
    re_ui_pointer(&g->ui, frame, ev->row, ev->col, RE_UI_DOWN);
}

static bool ensure_frame(re_arena_t *arena, re_draw_t *draw, re_screen_t *frame, uint16_t rows,
                         uint16_t cols) {
    if (!re_draw_init(draw, arena, rows, cols))
        return false;
    return re_screen_init(frame, arena, &draw->out, rows, cols);
}

static void paint_frame(gui_loop_t *g, re_draw_t *draw, re_screen_t *frame, re_term_t *term) {
    re_gui_menu_ctx_t mc;
    size_t prev_focus = (size_t)g->cpu->focus;
    re_dbg_lock_drain();
    re_dbg_lock_acquire();
    re_cpu_refresh(g->cpu, g->session);
    re_dbg_lock_release();
    if (g->focus_idx != prev_focus && g->focus_idx < (size_t)RE_CPU_PANE_COUNT) {
        g->cpu->focus = (re_cpu_pane_t)g->focus_idx;
        re_cpu_mark_dirty_all(g->cpu);
    }
    g->focus_idx = (size_t)g->cpu->focus;
    if (g->repaint || g->menu_open >= 0)
        re_cpu_mark_dirty_all(g->cpu);
    re_cpu_paint(frame, g->cpu);
    menu_ctx_fill(g, &mc);
    re_gui_chrome_draw(frame, &mc);
    re_gui_menu_draw(frame, &mc);
    re_ui_clear(&g->ui);
    re_gui_menu_bind(&g->ui, &mc);
    frame->tui.color = g->color && re_tui_want_color();
    re_ui_mark(&g->ui, frame, (uint8_t)RE_ST_HOVER, (uint8_t)RE_ST_PRESS);
    re_focus_build(g->focus, frame);
    if (g->cpu->zones[g->cpu->focus])
        re_focus_set(g->focus, g->cpu->zones[g->cpu->focus]);
    if (g->repaint || !draw->valid)
        re_draw_full(draw, frame);
    else
        re_draw_frame(draw, frame);
    g->repaint = false;
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

static void run_pending(gui_loop_t *g) {
    if (g->pending == RE_ACT_NONE)
        return;
    session_act(g->cpu, g->session, g->pending);
    g->pending = RE_ACT_NONE;
}

static void gui_loop(re_arena_t *arena, re_draw_t *draw, re_screen_t *frame, gui_loop_t *g,
                     re_term_t *term) {
    while (!g->quit) {
        re_ev_t ev;
        paint_frame(g, draw, frame, term);
        if (!re_term_wait(term, &ev, 250u)) {
            uint16_t nr = 0, nc = 0;
            re_term_size(&nr, &nc);
            if (nr != frame->rows || nc != frame->cols) {
                resize_frame(arena, draw, frame);
                re_cpu_mark_dirty_all(g->cpu);
            }
            continue;
        }
        if (ev.kind == RE_EV_RESIZE) {
            resize_frame(arena, draw, frame);
            re_cpu_mark_dirty_all(g->cpu);
            continue;
        }
        if (ev.kind == RE_EV_MOUSE)
            feed_pointer(g, frame, &ev);
        else
            (void)handle_key(g, &ev);
        run_pending(g);
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
    re_theme_t theme;
    gui_loop_t g;
    uint16_t rows = 24, cols = 80;
    (void)argc;
    (void)argv;
    if (!re_term_open(&term, false)) {
        if (ctx && ctx->err)
            RE_ERR_SET(ctx->err, RE_E_USAGE, "gui needs a terminal on stdin and stdout");
        fprintf(stderr, "gui needs a terminal on both ends; try status or mcp\n");
        return 2;
    }
    g.session = ctx && ctx->session ? ctx->session : re_dbg_session_global();
    seed_session_path(g.session, path);
    re_arena_init(&arena, 256u * 1024u);
    re_term_size(&rows, &cols);
    if (!ensure_frame(&arena, &draw, &frame, rows, cols)) {
        re_term_close(&term);
        re_arena_free(&arena);
        return 2;
    }
    re_focus_init(&focus);
    re_cpu_init(&cpu);
    re_ui_init(&g.ui);
    g.cpu = &cpu;
    g.focus = &focus;
    g.quit = false;
    g.color = true;
    g.repaint = true;
    g.menu_open = -1;
    g.menu_zone = RE_SCREEN_ZONE_NONE;
    g.menu_n = 0;
    g.btn_zone = RE_SCREEN_ZONE_NONE;
    g.btn_n = 0;
    g.pending = RE_ACT_NONE;
    g.focus_idx = 0;
    load_ux(&theme, &g.keys, &cpu);
    note_mcp_endpoint(&cpu);
    re_dbg_lock_set_owner(true);
    gui_loop(&arena, &draw, &frame, &g, &term);
    re_dbg_lock_set_owner(false);
    re_mcp_side_stop();
    re_tui_styles_reset();
    re_term_close(&term);
    re_arena_free(&arena);
    return 0;
}
