// re_draw.c - the diff between two frames, and the escapes that express it.
// Module: cli (C11).
// Owns: re_draw.h. No I/O: bytes go into a buffer the caller flushes.
// Depends: re_draw.h, re_screen.h, re_strbuf.h.
#include "cli/screen/re_draw.h"

#define ESC "\x1b["

static void move_to(re_strbuf_t *out, uint16_t row, uint16_t col) {
    // One based, because that is what the cursor addressing sequence counts in, and
    // off by one here shows up as a frame drawn one row and column up, which is the
    // kind of bug that reads as somebody else's mistake.
    re_strbuf_appendf(out, ESC "%u;%uH", (unsigned)row + 1u, (unsigned)col + 1u);
}

static bool same(const re_cell_t *a, const re_cell_t *b) {
    if (a->cols != b->cols || a->style != b->style)
        return false;
    if (a->g[0] != b->g[0])
        return false;
    for (size_t i = 1; i < 5; i++)
        if (a->g[i] != b->g[i])
            return false;
    return true;
}

bool re_draw_init(re_draw_t *d, re_arena_t *a, uint16_t rows, uint16_t cols) {
    re_strbuf_init(&d->out, a);
    d->valid = false;
    d->cells = 0;
    return re_screen_init(&d->prev, a, &d->out, rows, cols);
}

void re_draw_free(re_draw_t *d) {
    d->valid = false;
    d->prev.cell = NULL;
    d->prev.rows = 0;
    d->prev.cols = 0;
}

bool re_draw_resize(re_draw_t *d, re_arena_t *a, uint16_t rows, uint16_t cols) {
    d->valid = false;
    if (!re_screen_init(&d->prev, a, &d->out, rows, cols))
        return false;
    // Blank the new frame rather than trusting the arena to hand back zeroes. A cell
    // whose width field is indeterminate is read as the right half of a double width
    // glyph and skipped, which would leave the previous screen showing through.
    re_screen_clear(&d->prev);
    return true;
}

void re_draw_home(re_draw_t *d, uint16_t row, uint16_t col) {
    move_to(&d->out, row, col);
}

void re_draw_full(re_draw_t *d, const re_screen_t *cur) {
    re_screen_clear(&d->prev);
    d->valid = false;
    re_draw_frame(d, cur);
}

// One row: emit only the cells that differ. Returns how many were written.
static uint32_t paint_row(re_draw_t *d, const re_screen_t *cur, uint16_t row, uint8_t *last_style,
                          bool styled) {
    uint32_t wrote = 0;
    for (uint16_t col = 0; col < cur->cols;) {
        const re_cell_t *c = &cur->cell[(size_t)row * cur->cols + col];
        if (c->cols == 0) {
            // The right half of a double width glyph. The left half already moved the
            // cursor over it, so advancing is all that is left to do.
            col++;
            continue;
        }
        const re_cell_t *p = &d->prev.cell[(size_t)row * d->prev.cols + col];
        if (same(c, p)) {
            col++;
            continue;
        }
        move_to(&d->out, row, col);
        if (styled && c->style != *last_style) {
            // Reset first and set the one attribute wanted. A run of styles rather than
            // one escape per attribute is what keeps the output readable when a whole
            // screen changes at once.
            re_strbuf_puts(&d->out, ESC "0m");
            if (c->style != RE_ST_NONE)
                re_strbuf_puts(&d->out, re_tui_style_on((re_style_t)c->style));
            *last_style = c->style;
        }
        if (c->g[0])
            re_strbuf_puts(&d->out, c->g);
        else
            re_strbuf_putc(&d->out, ' ');
        wrote++;
        col++;
    }
    return wrote;
}

// Take the finished frame as the one to compare against next time. The glyphs are
// copied because they are small and fixed width; there are no pointers to chase.
static void absorb(re_draw_t *d, const re_screen_t *cur) {
    for (size_t i = 0, n = (size_t)cur->rows * cur->cols; i < n; i++) {
        re_cell_t *dst = &d->prev.cell[i];
        const re_cell_t *src = &cur->cell[i];
        dst->cols = src->cols;
        dst->style = src->style;
        dst->zone = src->zone;
        for (size_t k = 0; k < 5; k++)
            dst->g[k] = src->g[k];
    }
}

void re_draw_frame(re_draw_t *d, const re_screen_t *cur) {
    re_strbuf_clear(&d->out);
    d->cells = 0;
    if (!d->prev.cell || !cur->cell)
        return;
    // A geometry change cannot be diffed against, so it becomes a full paint rather
    // than a patch computed from two unrelated grids.
    if (d->prev.rows != cur->rows || d->prev.cols != cur->cols) {
        d->valid = false;
        re_screen_clear(&d->prev);
    }
    if (!d->valid) {
        // Clear, then hide the cursor for the duration: a visible cursor jumping around
        // inside a repaint is the flicker that makes a full screen feel slow.
        re_strbuf_puts(&d->out, ESC "2J");
        re_strbuf_puts(&d->out, ESC "?25l");
        d->valid = true;
    }
    bool styled = cur->tui.color;
    uint8_t last_style = 0xff;
    for (uint16_t row = 0; row < cur->rows; row++)
        d->cells += paint_row(d, cur, row, &last_style, styled);
    // An idle frame that still emits a reset touches the terminal for nothing, and
    // that touch is the flicker. The reset belongs to a frame that painted.
    if (styled && d->cells)
        re_strbuf_puts(&d->out, ESC "0m");
    absorb(d, cur);
}
