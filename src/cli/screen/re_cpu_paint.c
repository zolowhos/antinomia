// re_cpu_paint.c - incremental CPU pane layout and screen compose.
// Module: cli (C11).
// Owns: re_cpu_paint and per-pane rect layout. No terminal I/O.
// Depends: re_cpu.h, re_gui_menu (chrome height), re_screen. Cell-level diff stays in re_draw.
#include "cli/screen/re_cpu.h"

#include "cli/screen/menu/re_gui_menu.h"

typedef struct {
    uint16_t y;
    uint16_t x;
    uint16_t w;
    uint16_t h;
} re_cpu_rect_t;

static void paint_pane(re_screen_t *s, re_cpu_view_t *v, re_cpu_pane_t pane, re_cpu_rect_t r,
                       const char *title, const re_cpu_line_t *lines, size_t n, size_t hi) {
    uint8_t zone;
    uint8_t border;
    uint16_t body_h;
    uint16_t i;
    if (r.w < 4 || r.h < 3)
        return;
    zone = re_screen_zone(s);
    v->zones[pane] = zone;
    border = (v->focus == pane) ? (uint8_t)RE_ST_ACCENT : (uint8_t)RE_ST_NONE;
    re_screen_box(s, r.y, r.x, r.w, r.h, title, border);
    if (title && title[0] && r.w > 4u) {
        uint16_t tx = (uint16_t)(r.x + 2u);
        uint16_t edge = (uint16_t)(r.x + r.w - 2u);
        const char *p = title;
        while (*p && tx < edge) {
            char g[2] = {*p, 0};
            re_screen_put(s, r.y, tx, g, (uint8_t)RE_ST_TITLE, zone);
            p++;
            tx++;
        }
    }
    body_h = (uint16_t)(r.h - 2u);
    for (i = 0; i < body_h; i++) {
        uint16_t y = (uint16_t)(r.y + 1u + i);
        uint16_t x0 = (uint16_t)(r.x + 1u);
        uint16_t lim = (uint16_t)(r.x + r.w - 1u);
        uint8_t st = (uint8_t)RE_ST_NONE;
        re_screen_fill(s, y, x0, (uint16_t)(r.w - 2u), 1, " ", st, zone);
        if (i >= n)
            continue;
        if (pane == RE_CPU_PANE_DISASM && i == hi)
            st = (uint8_t)RE_ST_ACCENT;
        re_screen_put_run(s, y, (uint16_t)(x0 + 1u), lim, lines[i].text, st, zone);
    }
}

static void layout_rects(uint16_t rows, uint16_t cols, re_cpu_rect_t out[RE_CPU_PANE_COUNT]) {
    uint16_t mid_x = (uint16_t)(cols / 2u);
    uint16_t q = (uint16_t)(cols / 6u);
    uint16_t top_h;
    uint16_t mid_h;
    uint16_t bot_h;
    uint16_t y0 = RE_GUI_CHROME_TOP;
    uint16_t body;
    uint16_t y1;
    uint16_t y2;
    if (rows < 16u)
        rows = 16u;
    if (cols < 60u)
        cols = 60u;
    if (q < 8u)
        q = 8u;
    /* Menu + button chrome on top, status on the bottom edge. */
    body = (uint16_t)(rows - RE_GUI_CHROME_TOP - 1u);
    top_h = (uint16_t)(body * 36u / 100u);
    if (top_h < 4u)
        top_h = 4u;
    mid_h = (uint16_t)(body * 32u / 100u);
    if (mid_h < 4u)
        mid_h = 4u;
    bot_h = (uint16_t)(body - top_h - mid_h);
    if (bot_h < 3u) {
        bot_h = 3u;
        if (top_h + mid_h + bot_h > body) {
            mid_h = 4u;
            top_h = (uint16_t)(body - mid_h - bot_h);
        }
    }
    y1 = (uint16_t)(y0 + top_h);
    y2 = (uint16_t)(y1 + mid_h);
    out[RE_CPU_PANE_DISASM] = (re_cpu_rect_t){y0, 0, mid_x, top_h};
    out[RE_CPU_PANE_REGS] = (re_cpu_rect_t){y0, mid_x, (uint16_t)(cols - mid_x), top_h};
    out[RE_CPU_PANE_DUMP] = (re_cpu_rect_t){y1, 0, mid_x, mid_h};
    out[RE_CPU_PANE_STACK] = (re_cpu_rect_t){y1, mid_x, (uint16_t)(cols - mid_x), mid_h};
    out[RE_CPU_PANE_MAPS] = (re_cpu_rect_t){y2, 0, q, bot_h};
    out[RE_CPU_PANE_THREADS] = (re_cpu_rect_t){y2, q, q, bot_h};
    out[RE_CPU_PANE_BP] = (re_cpu_rect_t){y2, (uint16_t)(q * 2u), q, bot_h};
    out[RE_CPU_PANE_GRAPH] = (re_cpu_rect_t){y2, (uint16_t)(q * 3u), q, bot_h};
    out[RE_CPU_PANE_LOG] = (re_cpu_rect_t){y2, (uint16_t)(q * 4u), q, bot_h};
    out[RE_CPU_PANE_TRACE] =
        (re_cpu_rect_t){y2, (uint16_t)(q * 5u), (uint16_t)(cols - q * 5u), bot_h};
}

void re_cpu_paint(re_screen_t *s, re_cpu_view_t *v) {
    re_cpu_rect_t R[RE_CPU_PANE_COUNT];
    unsigned i;
    uint32_t mask;
    bool full;
    if (!s || !v || s->rows < 8u || s->cols < 20u)
        return;
    full = v->paint_full || (v->dirty & RE_CPU_DIRTY_ALL) == RE_CPU_DIRTY_ALL;
    mask = full ? (RE_CPU_DIRTY_ALL | RE_CPU_DIRTY_STATUS) : v->dirty;
    if (mask == 0)
        mask = RE_CPU_DIRTY_STATUS;
    if (full) {
        re_screen_clear(s);
        for (i = 0; i < (unsigned)RE_CPU_PANE_COUNT; i++)
            v->zones[i] = RE_SCREEN_ZONE_NONE;
    }
    layout_rects(s->rows, s->cols, R);
    if (mask & (1u << (unsigned)RE_CPU_PANE_DISASM))
        paint_pane(s, v, RE_CPU_PANE_DISASM, R[RE_CPU_PANE_DISASM], "disasm @ rip", v->disasm,
                   v->n_disasm, v->rip_row);
    if (mask & (1u << (unsigned)RE_CPU_PANE_REGS))
        paint_pane(s, v, RE_CPU_PANE_REGS, R[RE_CPU_PANE_REGS], "registers", v->reg_lines,
                   v->n_regs, (size_t)-1);
    if (mask & (1u << (unsigned)RE_CPU_PANE_DUMP))
        paint_pane(s, v, RE_CPU_PANE_DUMP, R[RE_CPU_PANE_DUMP], "dump", v->dump, v->n_dump,
                   (size_t)-1);
    if (mask & (1u << (unsigned)RE_CPU_PANE_STACK))
        paint_pane(s, v, RE_CPU_PANE_STACK, R[RE_CPU_PANE_STACK], "call stack", v->stack,
                   v->n_stack, (size_t)-1);
    if (mask & (1u << (unsigned)RE_CPU_PANE_MAPS))
        paint_pane(s, v, RE_CPU_PANE_MAPS, R[RE_CPU_PANE_MAPS], "maps", v->map_lines, v->n_maps,
                   (size_t)-1);
    if (mask & (1u << (unsigned)RE_CPU_PANE_THREADS))
        paint_pane(s, v, RE_CPU_PANE_THREADS, R[RE_CPU_PANE_THREADS], "threads", v->thr_lines,
                   v->n_threads, (size_t)-1);
    if (mask & (1u << (unsigned)RE_CPU_PANE_BP))
        paint_pane(s, v, RE_CPU_PANE_BP, R[RE_CPU_PANE_BP], "breakpoints", v->bp_lines,
                   v->n_bp_lines, (size_t)-1);
    if (mask & (1u << (unsigned)RE_CPU_PANE_GRAPH))
        paint_pane(s, v, RE_CPU_PANE_GRAPH, R[RE_CPU_PANE_GRAPH], "graph", v->graph, v->n_graph,
                   (size_t)-1);
    if (mask & (1u << (unsigned)RE_CPU_PANE_LOG))
        paint_pane(s, v, RE_CPU_PANE_LOG, R[RE_CPU_PANE_LOG], "log", v->log, v->n_log, (size_t)-1);
    if (mask & (1u << (unsigned)RE_CPU_PANE_TRACE))
        paint_pane(s, v, RE_CPU_PANE_TRACE, R[RE_CPU_PANE_TRACE], "trace", v->trace_lines,
                   v->n_trace, (size_t)-1);
    if (mask & RE_CPU_DIRTY_STATUS)
        re_screen_status(s, (uint16_t)(s->rows - 1u), 0, s->cols, v->status_left, v->status_right);
    v->dirty = 0;
    v->paint_full = false;
}
