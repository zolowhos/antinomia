// re_screen_widgets.c - the widgets, and the two ways the grid is turned into text.
// Module: util (C11).
// Owns: tabs, legend, toolbar, nav, gutter, scrollbars, status, list, code, draw, dump.
// Depends: re_screen.h, re_strbuf, re_tui. Splits from re_screen.c only on the 500
//           line cap; the two share nothing but the grid.
#include "utils/tui/re_screen.h"

void re_screen_tabs(re_screen_t *s, uint16_t y, uint16_t x, uint16_t w, const char *const *labels,
                    size_t n, size_t active) {
    if (!re_screen_inside(s, y, x) || w < 4)
        return;
    uint16_t cx = x;
    uint16_t limit = (uint16_t)(x + w);
    for (size_t i = 0; i < n && cx < limit; i++) {
        const char *label = labels[i] ? labels[i] : "";
        size_t lc = re_tui_cols(label);
        // Every tab is its own clickable control, so a click on a tab can be resolved
        // without the layout having to remember where it put them.
        uint8_t zone = re_screen_zone(s);
        uint8_t st = (i == active) ? (uint8_t)RE_ST_ACCENT : (uint8_t)RE_ST_NONE;
        re_screen_fill(s, y, cx, 1, 1, " ", st, zone);
        cx++;
        cx = re_screen_put_run(s, y, cx, limit, label, st, zone);
        if (cx < limit) {
            re_screen_fill(s, y, cx, 1, 1, " ", st, zone);
            cx++;
        }
        // A separator between tabs, the way a strip reads as separate stops.
        if (i + 1 < n && cx < limit) {
            re_screen_put(s, y, cx, "|", (uint8_t)RE_ST_NONE, RE_SCREEN_ZONE_NONE);
            cx++;
        }
        (void)lc;
    }
}

void re_screen_legend(re_screen_t *s, uint16_t y, uint16_t x, uint16_t w, const char *const *labels,
                      size_t n) {
    if (!re_screen_inside(s, y, x) || w < 4)
        return;
    uint16_t cx = x;
    uint16_t limit = (uint16_t)(x + w);
    for (size_t i = 0; i < n && cx < limit; i++) {
        // The swatch is the shape, not a colour: a block between two spaces is what
        // the row is saying, and it survives a monochrome terminal intact.
        re_screen_fill(s, y, cx, 3, 1, " ", (uint8_t)RE_ST_NONE, RE_SCREEN_ZONE_NONE);
        if (cx < limit)
            re_screen_put(s, y, cx, "\xe2\x96\xaa", (uint8_t)RE_ST_LABEL, RE_SCREEN_ZONE_NONE);
        cx += 3;
        cx = re_screen_put_run(s, y, cx, limit, labels[i] ? labels[i] : "", (uint8_t)RE_ST_NONE,
                               RE_SCREEN_ZONE_NONE);
        if (i + 1 < n && cx < limit) {
            re_screen_fill(s, y, cx, 1, 1, " ", (uint8_t)RE_ST_NONE, RE_SCREEN_ZONE_NONE);
            cx++;
        }
    }
}

void re_screen_toolbar(re_screen_t *s, uint16_t y, const char *const *items, size_t n) {
    if (!s->rows || y >= s->rows)
        return;
    uint16_t cx = 0;
    uint16_t limit = s->cols;
    for (size_t i = 0; i < n && cx < limit; i++) {
        uint8_t zone = re_screen_zone(s);
        // One column of padding either side is what turns a label into something that
        // reads as pressable. The padding is drawn after the label and is inside the
        // same zone, so a click on it still lands on the control it belongs to.
        re_screen_fill(s, y, cx, 1, 1, " ", (uint8_t)RE_ST_NONE, zone);
        uint16_t end = re_screen_put_run(s, y, (uint16_t)(cx + 1), limit, items[i] ? items[i] : "",
                                         (uint8_t)RE_ST_NONE, zone);
        if (end > cx + 1u && end < limit)
            re_screen_fill(s, y, end, 1, 1, " ", (uint8_t)RE_ST_NONE, zone);
        // The column just past the item is where the next one starts, so the trailing
        // space has to be written even when the separator does not fit after it.
        cx = end;
        if (cx + 1u >= limit)
            break;
        cx++;
        re_screen_put(s, y, cx, "|", (uint8_t)RE_ST_NONE, RE_SCREEN_ZONE_NONE);
        cx = (uint16_t)(cx + 2u);
    }
}

void re_screen_nav(re_screen_t *s, uint16_t y, uint16_t x, uint16_t w, size_t pos, size_t total) {
    if (!re_screen_inside(s, y, x) || w < 8)
        return;
    re_screen_put(s, y, x, "<", (uint8_t)RE_ST_NONE, RE_SCREEN_ZONE_NONE);
    re_screen_put(s, y, (uint16_t)(x + w - 1), ">", (uint8_t)RE_ST_NONE, RE_SCREEN_ZONE_NONE);
    uint16_t bar_x = (uint16_t)(x + 2);
    uint16_t bar_w = (uint16_t)(w - 4);
    re_screen_fill(s, y, bar_x, bar_w, 1, " ", (uint8_t)RE_ST_NONE, RE_SCREEN_ZONE_NONE);
    if (!total)
        return;
    // The handle is where the reader is, as a fraction of the track, and never wider
    // than the track: a handle that overflows is a scrollbar that lies about position.
    uint16_t handle = (uint16_t)((bar_w * pos) / total);
    if (handle >= bar_w)
        handle = (uint16_t)(bar_w - 1);
    re_screen_fill(s, y, (uint16_t)(bar_x + handle), 1, 1, "\xe2\x96\x88", (uint8_t)RE_ST_ACCENT,
                   RE_SCREEN_ZONE_NONE);
}

// Decimal, into a caller supplied buffer, returning the length. Written out rather
// than taken from a formatter because the gutter needs the digits right aligned in a
// fixed eight column strip on every call, and a general formatter would carry a
// buffer and an arena to do something this does in a loop over ten digits.
static size_t put_dec(char *dst, size_t cap, uint32_t v) {
    char tmp[12];
    size_t n = 0;
    do {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v && n < sizeof(tmp));
    if (n >= cap)
        n = cap - 1;
    for (size_t i = 0; i < n; i++)
        dst[i] = tmp[n - 1 - i];
    dst[n] = '\0';
    return n;
}

void re_screen_gutter(re_screen_t *s, uint16_t y, uint16_t x, uint16_t h, uint32_t first,
                      const uint8_t *marks, size_t n) {
    if (!re_screen_inside(s, y, x) || !h)
        return;
    for (uint16_t r = 0; r < h; r++) {
        size_t idx = r;
        if (idx >= n)
            break;
        char num[12];
        size_t len = put_dec(num, sizeof(num), first + (uint32_t)idx);
        // Right aligned in the seven columns before the mark column, so the digits
        // sit next to the code instead of drifting against a wide left pane.
        char pad[10];
        size_t pl = 0;
        for (size_t i = len; i < 7; i++)
            pad[pl++] = ' ';
        pad[pl] = '\0';
        re_screen_put_run(s, (uint16_t)(y + r), x, (uint16_t)(x + 7), pad, (uint8_t)RE_ST_NONE,
                          RE_SCREEN_ZONE_NONE);
        re_screen_put_run(s, (uint16_t)(y + r), (uint16_t)(x + 7 - len), (uint16_t)(x + 8), num,
                          (uint8_t)RE_ST_NONE, RE_SCREEN_ZONE_NONE);
        // The dot column is the dataflow hint: a mark on a line means that line
        // defines or uses something worth following. It is one column, right of the
        // numbers, and never shifts the text that follows it.
        re_screen_fill(s, (uint16_t)(y + r), (uint16_t)(x + 7), 1, 1, " ", (uint8_t)RE_ST_NONE,
                       RE_SCREEN_ZONE_NONE);
        if (marks[idx])
            re_screen_put(s, (uint16_t)(y + r), (uint16_t)(x + 7), "\xe2\x80\xa2",
                          (uint8_t)RE_ST_ACCENT, RE_SCREEN_ZONE_NONE);
    }
}

void re_screen_vscroll(re_screen_t *s, uint16_t y, uint16_t x, uint16_t h, uint16_t pos,
                       uint16_t total) {
    if (!re_screen_inside(s, y, x) || !h)
        return;
    re_screen_fill(s, y, x, 1, h, "\xe2\x94\x82", (uint8_t)RE_ST_NONE, RE_SCREEN_ZONE_NONE);
    if (!total || h < 3)
        return;
    uint16_t track = (uint16_t)(h - 2);
    uint16_t handle = (uint16_t)((track * pos) / total);
    if (handle >= track)
        handle = (uint16_t)(track - 1);
    re_screen_put(s, y, x, "\xe2\x96\xb2", (uint8_t)RE_ST_NONE, RE_SCREEN_ZONE_NONE);
    re_screen_put(s, (uint16_t)(y + h - 1), x, "\xe2\x96\xbc", (uint8_t)RE_ST_NONE,
                  RE_SCREEN_ZONE_NONE);
    re_screen_fill(s, (uint16_t)(y + handle + 1), x, 1, 1, "\xe2\x96\x88", (uint8_t)RE_ST_ACCENT,
                   RE_SCREEN_ZONE_NONE);
}

void re_screen_status(re_screen_t *s, uint16_t y, uint16_t x, uint16_t w, const char *left,
                      const char *right) {
    if (!re_screen_inside(s, y, x) || w < 4)
        return;
    re_screen_fill(s, y, x, w, 1, " ", (uint8_t)RE_ST_NONE, RE_SCREEN_ZONE_NONE);
    uint16_t cx = re_screen_put_run(s, y, (uint16_t)(x + 1), (uint16_t)(x + w - 1),
                                    left ? left : "", (uint8_t)RE_ST_HEAD, RE_SCREEN_ZONE_NONE);
    (void)cx;
    if (!right || !*right)
        return;
    size_t rc = re_tui_cols(right);
    if (rc + 2 >= w)
        return;
    re_screen_put_run(s, y, (uint16_t)(x + w - 1 - rc), (uint16_t)(x + w), right,
                      (uint8_t)RE_ST_NONE, RE_SCREEN_ZONE_NONE);
}

void re_screen_list(re_screen_t *s, uint16_t y, uint16_t x, uint16_t w, uint16_t h,
                    const char *head, const char *const *rows, size_t n, size_t sel) {
    if (!re_screen_inside(s, y, x) || !h)
        return;
    re_screen_put_run(s, y, (uint16_t)(x + 1), (uint16_t)(x + w), head ? head : "",
                      (uint8_t)RE_ST_HEAD, RE_SCREEN_ZONE_NONE);
    for (uint16_t r = 0; r + 1 < h; r++) {
        size_t i = r;
        if (i >= n)
            break;
        uint8_t st = (i == sel) ? (uint8_t)RE_ST_ACCENT : (uint8_t)RE_ST_NONE;
        uint8_t zone = re_screen_zone(s);
        re_screen_fill(s, (uint16_t)(y + 1 + r), x, w, 1, " ", st, zone);
        // One space of indent, then the marker column, so a row reads as an item in
        // a list rather than as a bare line of text.
        re_screen_put_run(s, (uint16_t)(y + 1 + r), (uint16_t)(x + 1), (uint16_t)(x + w),
                          rows[i] ? rows[i] : "", st, zone);
    }
}

void re_screen_code(re_screen_t *s, uint16_t y, uint16_t x, uint16_t w, uint16_t h,
                    const char *const *lines, size_t n, uint32_t base, const uint8_t *marks) {
    if (!re_screen_inside(s, y, x) || !h)
        return;
    // The last column is reserved for the marker. Without the reservation the marker
    // lands on top of the final character, which is how a line of code ends up
    // missing its semicolon every time it happens to carry a mark.
    uint16_t text_w = w > 1u ? (uint16_t)(w - 1u) : 0u;
    for (uint16_t r = 0; r < h; r++) {
        size_t i = r;
        if (i >= n)
            break;
        (void)base;
        re_screen_put_run(s, (uint16_t)(y + r), x, (uint16_t)(x + text_w), lines[i] ? lines[i] : "",
                          (uint8_t)RE_ST_NONE, RE_SCREEN_ZONE_NONE);
        if (marks && marks[i] && w > 1u)
            re_screen_put(s, (uint16_t)(y + r), (uint16_t)(x + w - 1), "\xe2\x96\xb8",
                          (uint8_t)RE_ST_ACCENT, RE_SCREEN_ZONE_NONE);
    }
}

void re_screen_draw(const re_screen_t *s) {
    if (!s->cell || !s->out)
        return;
    for (uint16_t y = 0; y < s->rows; y++) {
        for (uint16_t x = 0; x < s->cols; x++) {
            const re_cell_t *c = &s->cell[(size_t)y * s->cols + x];
            if (c->cols == 0)
                continue; // right half of a double width glyph, already drawn
            if (!c->g[0]) {
                re_strbuf_putc(s->out, ' ');
                continue;
            }
            re_tui_styled(s->out, &s->tui, (re_style_t)c->style, c->g);
        }
        re_strbuf_putc(s->out, '\n');
    }
}

void re_screen_dump(const re_screen_t *s, re_strbuf_t *dst) {
    if (!s->cell)
        return;
    for (uint16_t y = 0; y < s->rows; y++) {
        // A row is emitted only as far as its last non blank cell. Trailing spaces
        // carry no information in a dump and make every assertion about it noisier.
        uint16_t last = 0;
        for (uint16_t x = 0; x < s->cols; x++)
            if (s->cell[(size_t)y * s->cols + x].g[0])
                last = (uint16_t)(x + 1);
        for (uint16_t x = 0; x < last; x++) {
            const re_cell_t *c = &s->cell[(size_t)y * s->cols + x];
            // A blank cell is a space, not a hole. Skipping it collapses every
            // interior gap, which moves whatever follows leftwards in the dump while
            // it stays where it is on screen, and a dump that misreports where
            // anything is cannot be used to check position at all. Only the trailing
            // run is dropped.
            if (c->cols == 0 || !c->g[0])
                re_strbuf_putc(dst, ' ');
            else
                re_strbuf_puts(dst, c->g);
        }
        re_strbuf_putc(dst, '\n');
    }
}
