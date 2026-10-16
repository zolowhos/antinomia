// re_cpu.c - fills and paints the CPU-like debugger panes from session state.
// Module: cli (C11).
// Owns: re_cpu.h formatting and layout. No terminal I/O.
// Depends: re_cpu.h, re_screen, re_hex. Backend stubs yield placeholder lines.
#include "cli/screen/re_cpu.h"

#include "utils/text/re_hex.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void copy_cstr(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    if (!dst || cap == 0)
        return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void line_set(re_cpu_line_t *L, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(L->text, sizeof(L->text), fmt, ap);
    va_end(ap);
}

void re_cpu_init(re_cpu_view_t *v) {
    memset(v, 0, sizeof(*v));
    v->focus = RE_CPU_PANE_DISASM;
    v->dump_addr = 0;
    copy_cstr(v->status_right, sizeof(v->status_right), "tab focus  q quit");
}

void re_cpu_set_bps(re_cpu_view_t *v, const re_cpu_bp_t *bps, size_t n) {
    size_t i;
    if (!v)
        return;
    if (!bps)
        n = 0;
    if (n > RE_CPU_BP_MAX)
        n = RE_CPU_BP_MAX;
    v->n_bps = n;
    for (i = 0; i < n; i++)
        v->bps[i] = bps[i];
}

void re_cpu_log_push(re_cpu_view_t *v, const char *msg) {
    size_t i;
    if (!v || !msg)
        return;
    if (v->n_log < RE_CPU_LOG_MAX) {
        copy_cstr(v->log[v->n_log].text, RE_CPU_LINE_CAP, msg);
        v->n_log++;
        return;
    }
    for (i = 1; i < RE_CPU_LOG_MAX; i++)
        v->log[i - 1] = v->log[i];
    copy_cstr(v->log[RE_CPU_LOG_MAX - 1].text, RE_CPU_LINE_CAP, msg);
}

void re_cpu_focus_next(re_cpu_view_t *v) {
    v->focus = (re_cpu_pane_t)(((unsigned)v->focus + 1u) % (unsigned)RE_CPU_PANE_COUNT);
}

void re_cpu_focus_prev(re_cpu_view_t *v) {
    unsigned f = (unsigned)v->focus;
    v->focus =
        (re_cpu_pane_t)((f + (unsigned)RE_CPU_PANE_COUNT - 1u) % (unsigned)RE_CPU_PANE_COUNT);
}

bool re_cpu_focus_zone(re_cpu_view_t *v, uint8_t zone) {
    unsigned i;
    if (!zone)
        return false;
    for (i = 0; i < (unsigned)RE_CPU_PANE_COUNT; i++) {
        if (v->zones[i] == zone) {
            v->focus = (re_cpu_pane_t)i;
            return true;
        }
    }
    return false;
}

static void pull_regs(re_cpu_view_t *v, re_dbg_session_t *s) {
    re_err_code_t c;
    v->have_regs = false;
    memset(&v->regs, 0, sizeof(v->regs));
    v->rip = s->last_event.addr;
    v->rsp = 0;
    if (!s->attached || !s->backend || !s->backend->vt || !s->backend->vt->get_regs)
        return;
    c = s->backend->vt->get_regs(s->backend, s->tid, &v->regs);
    if (c != RE_OK)
        return;
    v->have_regs = true;
    v->rip = v->regs.rip;
    v->rsp = v->regs.rsp;
}

static size_t try_read(re_dbg_session_t *s, uint64_t addr, uint8_t *dst, size_t n) {
    if (!s->attached || !s->backend || !s->backend->vt || !s->backend->vt->read_mem)
        return 0;
    if (s->backend->vt->read_mem(s->backend, addr, dst, n) != RE_OK)
        return 0;
    return n;
}

static void fill_regs_lines(re_cpu_view_t *v) {
    const re_dbg_regs_t *r = &v->regs;
    v->n_regs = 0;
    if (!v->have_regs) {
        line_set(&v->reg_lines[v->n_regs++], "(regs unavailable)");
        line_set(&v->reg_lines[v->n_regs++], "RAX ----------------");
        line_set(&v->reg_lines[v->n_regs++], "RBX ----------------");
        line_set(&v->reg_lines[v->n_regs++], "RCX ----------------");
        line_set(&v->reg_lines[v->n_regs++], "RDX ----------------");
        line_set(&v->reg_lines[v->n_regs++], "RSI ----------------");
        line_set(&v->reg_lines[v->n_regs++], "RDI ----------------");
        line_set(&v->reg_lines[v->n_regs++], "RBP ----------------");
        line_set(&v->reg_lines[v->n_regs++], "RSP ----------------");
        line_set(&v->reg_lines[v->n_regs++], "RIP ----------------");
        return;
    }
    line_set(&v->reg_lines[v->n_regs++], "RAX %016llx", (unsigned long long)r->rax);
    line_set(&v->reg_lines[v->n_regs++], "RBX %016llx", (unsigned long long)r->rbx);
    line_set(&v->reg_lines[v->n_regs++], "RCX %016llx", (unsigned long long)r->rcx);
    line_set(&v->reg_lines[v->n_regs++], "RDX %016llx", (unsigned long long)r->rdx);
    line_set(&v->reg_lines[v->n_regs++], "RSI %016llx", (unsigned long long)r->rsi);
    line_set(&v->reg_lines[v->n_regs++], "RDI %016llx", (unsigned long long)r->rdi);
    line_set(&v->reg_lines[v->n_regs++], "RBP %016llx", (unsigned long long)r->rbp);
    line_set(&v->reg_lines[v->n_regs++], "RSP %016llx", (unsigned long long)r->rsp);
    line_set(&v->reg_lines[v->n_regs++], "R8  %016llx", (unsigned long long)r->r8);
    line_set(&v->reg_lines[v->n_regs++], "R9  %016llx", (unsigned long long)r->r9);
    line_set(&v->reg_lines[v->n_regs++], "R10 %016llx", (unsigned long long)r->r10);
    line_set(&v->reg_lines[v->n_regs++], "R11 %016llx", (unsigned long long)r->r11);
    line_set(&v->reg_lines[v->n_regs++], "R12 %016llx", (unsigned long long)r->r12);
    line_set(&v->reg_lines[v->n_regs++], "R13 %016llx", (unsigned long long)r->r13);
    line_set(&v->reg_lines[v->n_regs++], "R14 %016llx", (unsigned long long)r->r14);
    line_set(&v->reg_lines[v->n_regs++], "R15 %016llx", (unsigned long long)r->r15);
    line_set(&v->reg_lines[v->n_regs++], "RIP %016llx", (unsigned long long)r->rip);
    line_set(&v->reg_lines[v->n_regs++], "FLG %016llx", (unsigned long long)r->rflags);
}

static void fill_disasm(re_cpu_view_t *v, re_dbg_session_t *s) {
    size_t i;
    const size_t n = 16u;
    const size_t mid = n / 2u;
    uint64_t base = v->rip ? (v->rip > mid ? v->rip - mid : 0u) : 0u;
    v->n_disasm = n;
    v->rip_row = mid;
    v->have_mem = false;
    for (i = 0; i < n; i++) {
        uint8_t bytes[4];
        uint64_t addr = base + i;
        char hex[16];
        size_t got = try_read(s, addr, bytes, sizeof(bytes));
        if (got) {
            v->have_mem = true;
            re_hex_encode(hex, bytes, 4);
            line_set(&v->disasm[i], "%016llx  %s  db", (unsigned long long)addr, hex);
        } else if (v->attached) {
            line_set(&v->disasm[i], "%016llx  ?? ?? ?? ??  ; mem stub", (unsigned long long)addr);
        } else {
            line_set(&v->disasm[i], "%016llx  -- -- -- --  ; no target", (unsigned long long)addr);
        }
        if (addr == v->rip)
            v->rip_row = i;
    }
}

static void fill_dump(re_cpu_view_t *v, re_dbg_session_t *s) {
    size_t i;
    v->n_dump = 8u;
    if (!v->dump_addr)
        v->dump_addr = v->rip;
    for (i = 0; i < v->n_dump; i++) {
        uint8_t row[16];
        uint64_t addr = v->dump_addr + i * 16u;
        char hex[40];
        size_t got = try_read(s, addr, row, sizeof(row));
        if (got) {
            size_t b;
            size_t o = 0;
            for (b = 0; b < 16u && o + 3u < sizeof(hex); b++) {
                hex[o++] = re_hex_nibble((uint8_t)(row[b] >> 4));
                hex[o++] = re_hex_nibble(row[b]);
                hex[o++] = ' ';
            }
            hex[o ? o - 1u : 0] = 0;
            line_set(&v->dump[i], "%016llx  %s", (unsigned long long)addr, hex);
        } else {
            line_set(&v->dump[i], "%016llx  -- -- -- -- -- -- -- --", (unsigned long long)addr);
        }
    }
}

static void fill_stack(re_cpu_view_t *v, re_dbg_session_t *s) {
    size_t i;
    v->n_stack = 12u;
    for (i = 0; i < v->n_stack; i++) {
        uint8_t raw[8];
        uint64_t addr = v->rsp + i * 8u;
        uint64_t word = 0;
        size_t got = try_read(s, addr, raw, sizeof(raw));
        if (got) {
            size_t b;
            for (b = 0; b < 8u; b++)
                word |= (uint64_t)raw[b] << (8u * b);
            line_set(&v->stack[i], "rsp+%02zx  %016llx", i * 8u, (unsigned long long)word);
        } else if (v->have_regs) {
            line_set(&v->stack[i], "rsp+%02zx  ----------------", i * 8u);
        } else {
            line_set(&v->stack[i], "rsp+--  ----------------");
        }
    }
}

static void fill_bps(re_cpu_view_t *v) {
    size_t i;
    v->n_bp_lines = 0;
    if (!v->n_bps) {
        line_set(&v->bp_lines[v->n_bp_lines++], "(no breakpoints)");
        return;
    }
    for (i = 0; i < v->n_bps && v->n_bp_lines < RE_CPU_BP_MAX; i++) {
        line_set(&v->bp_lines[v->n_bp_lines++], "%c %016llx %s", v->bps[i].enabled ? '*' : ' ',
                 (unsigned long long)v->bps[i].addr, v->bps[i].note[0] ? v->bps[i].note : "");
    }
}

static void maybe_log_event(re_cpu_view_t *v, re_dbg_session_t *s) {
    char msg[RE_CPU_LINE_CAP];
    if (s->last_event.kind == RE_DBG_EV_NONE)
        return;
    if (s->last_event.kind == v->logged_kind && s->last_event.addr == v->logged_addr)
        return;
    v->logged_kind = s->last_event.kind;
    v->logged_addr = s->last_event.addr;
    (void)snprintf(msg, sizeof(msg), "%s pid=%u tid=%u @ %016llx",
                   re_dbg_event_kind_str(s->last_event.kind), (unsigned)s->last_event.pid,
                   (unsigned)s->last_event.tid, (unsigned long long)s->last_event.addr);
    re_cpu_log_push(v, msg);
}

static void fill_status(re_cpu_view_t *v) {
    const char *path = v->path && v->path[0] ? v->path : "-";
    (void)snprintf(v->status_left, sizeof(v->status_left), "pid=%u tid=%u event=%s @ %016llx  %s",
                   (unsigned)v->pid, (unsigned)v->tid, re_dbg_event_kind_str(v->last_kind),
                   (unsigned long long)v->last_addr, path);
    copy_cstr(v->status_right, sizeof(v->status_right), "tab focus  q quit");
}

void re_cpu_refresh(re_cpu_view_t *v, re_dbg_session_t *s) {
    if (!v || !s)
        return;
    v->attached = s->attached;
    v->pid = s->pid;
    v->tid = s->tid;
    v->path = s->path;
    v->last_kind = s->last_event.kind;
    v->last_addr = s->last_event.addr;
    pull_regs(v, s);
    if (!v->dump_addr)
        v->dump_addr = v->rip;
    fill_regs_lines(v);
    fill_disasm(v, s);
    fill_dump(v, s);
    fill_stack(v, s);
    fill_bps(v);
    maybe_log_event(v, s);
    if (v->n_log == 0)
        re_cpu_log_push(v, v->attached ? "session attached" : "no target - panes idle");
    fill_status(v);
}

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
    // Title cells keep the pane zone so clicks and Tab land on the frame.
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
    uint16_t top_h;
    uint16_t mid_h;
    uint16_t bot_h;
    uint16_t y1;
    uint16_t y2;
    uint16_t bp_w;
    if (rows < 12u)
        rows = 12u;
    if (cols < 40u)
        cols = 40u;
    // title(1) + body + status(1). Split body into three bands.
    top_h = (uint16_t)((rows - 2u) * 40u / 100u);
    if (top_h < 4u)
        top_h = 4u;
    mid_h = (uint16_t)((rows - 2u) * 35u / 100u);
    if (mid_h < 4u)
        mid_h = 4u;
    bot_h = (uint16_t)(rows - 2u - top_h - mid_h);
    if (bot_h < 3u) {
        bot_h = 3u;
        if (top_h + mid_h + bot_h + 2u > rows) {
            mid_h = 4u;
            top_h = (uint16_t)(rows - 2u - mid_h - bot_h);
        }
    }
    y1 = (uint16_t)(1u + top_h);
    y2 = (uint16_t)(y1 + mid_h);
    bp_w = (uint16_t)(cols / 3u);
    if (bp_w < 16u)
        bp_w = 16u;
    if (bp_w + 12u > cols)
        bp_w = (uint16_t)(cols / 2u);
    out[RE_CPU_PANE_DISASM] = (re_cpu_rect_t){1, 0, mid_x, top_h};
    out[RE_CPU_PANE_REGS] = (re_cpu_rect_t){1, mid_x, (uint16_t)(cols - mid_x), top_h};
    out[RE_CPU_PANE_DUMP] = (re_cpu_rect_t){y1, 0, mid_x, mid_h};
    out[RE_CPU_PANE_STACK] = (re_cpu_rect_t){y1, mid_x, (uint16_t)(cols - mid_x), mid_h};
    out[RE_CPU_PANE_BP] = (re_cpu_rect_t){y2, 0, bp_w, bot_h};
    out[RE_CPU_PANE_LOG] = (re_cpu_rect_t){y2, bp_w, (uint16_t)(cols - bp_w), bot_h};
}

void re_cpu_paint(re_screen_t *s, re_cpu_view_t *v) {
    re_cpu_rect_t R[RE_CPU_PANE_COUNT];
    unsigned i;
    if (!s || !v || s->rows < 8u || s->cols < 20u)
        return;
    re_screen_clear(s);
    for (i = 0; i < (unsigned)RE_CPU_PANE_COUNT; i++)
        v->zones[i] = RE_SCREEN_ZONE_NONE;
    re_screen_put_run(s, 0, 1, s->cols > 2u ? (uint16_t)(s->cols - 2u) : 0u, "antinomia - CPU view",
                      (uint8_t)RE_ST_TITLE, RE_SCREEN_ZONE_NONE);
    layout_rects(s->rows, s->cols, R);
    paint_pane(s, v, RE_CPU_PANE_DISASM, R[RE_CPU_PANE_DISASM], "disasm @ rip", v->disasm,
               v->n_disasm, v->rip_row);
    paint_pane(s, v, RE_CPU_PANE_REGS, R[RE_CPU_PANE_REGS], "registers", v->reg_lines, v->n_regs,
               (size_t)-1);
    paint_pane(s, v, RE_CPU_PANE_DUMP, R[RE_CPU_PANE_DUMP], "dump", v->dump, v->n_dump, (size_t)-1);
    paint_pane(s, v, RE_CPU_PANE_STACK, R[RE_CPU_PANE_STACK], "stack @ rsp", v->stack, v->n_stack,
               (size_t)-1);
    paint_pane(s, v, RE_CPU_PANE_BP, R[RE_CPU_PANE_BP], "breakpoints", v->bp_lines, v->n_bp_lines,
               (size_t)-1);
    paint_pane(s, v, RE_CPU_PANE_LOG, R[RE_CPU_PANE_LOG], "log", v->log, v->n_log, (size_t)-1);
    re_screen_status(s, (uint16_t)(s->rows - 1u), 0, s->cols, v->status_left, v->status_right);
}
