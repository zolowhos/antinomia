// re_screen.c - the cell grid, the widgets, and the two ways to render it.
// Module: util (C11).
// Owns: cell writes, the widgets in re_screen.h, and re_screen_draw/re_screen_dump.
// Depends: re_screen.h, re_arena, re_strbuf, re_tui. No input, no I/O policy, no
//           decision about what belongs on screen.
#include "utils/tui/re_screen.h"

#define ZONE_MAX 255u

static re_cell_t *at(re_screen_t *s, uint16_t y, uint16_t x) {
    return &s->cell[(size_t)y * s->cols + x];
}

bool re_screen_inside(const re_screen_t *s, uint16_t y, uint16_t x) {
    return y < s->rows && x < s->cols;
}

// How many display columns the next glyph in s occupies, and how long it is in bytes.
// A leading byte below 0x80 is one column, 0xC0..0xDF is two, 0xE0.. are three or
// four bytes wide and one column here, because the width that matters for layout is
// the width the terminal will draw, and a four byte sequence is not double width.
// Anything malformed is taken as one byte and one column rather than trusted.
static uint8_t glyph_cols(const char *g, size_t *n) {
    unsigned char c = (unsigned char)g[0];
    if (c == 0) {
        *n = 1;
        return 1;
    }
    if (c < 0x80) {
        *n = 1;
        return 1;
    }
    if (c < 0xC0) {
        *n = 1;
        return 0; // a continuation byte reached on its own: skip, do not draw
    }
    if (c < 0xE0) {
        *n = 2;
        return 1;
    }
    if (c < 0xF0) {
        *n = 3;
        return 1;
    }
    *n = 4;
    return 1;
}

bool re_screen_init(re_screen_t *s, re_arena_t *a, re_strbuf_t *out, uint16_t rows, uint16_t cols) {
    s->rows = 0;
    s->cols = 0;
    s->cell = NULL;
    s->out = out;
    s->cy = 0;
    s->cx = 0;
    s->zone_count = 0;
    s->overflow = false;
    if (!rows || !cols || rows > RE_SCREEN_MAX_ROWS || cols > RE_SCREEN_MAX_COLS) {
        s->overflow = true;
        return false;
    }
    s->cell = (re_cell_t *)re_arena_calloc(a, (size_t)rows * cols, sizeof(re_cell_t));
    if (!s->cell) {
        s->overflow = true;
        return false;
    }
    s->rows = rows;
    s->cols = cols;
    re_tui_init(&s->tui, out, out, false, false, cols);
    s->tui.unicode = re_tui_want_unicode();
    return true;
}

// A whole run of text into a row, advancing the column and stopping at limit. The
// glyph-by-glyph put is the primitive; this is what every caller that has a string
// actually wants, and putting the burden of decoding UTF-8 on each of them is how a
// name ends up drawn as its first letter.
uint16_t re_screen_put_run(re_screen_t *s, uint16_t y, uint16_t x, uint16_t limit, const char *p,
                           uint8_t style, uint8_t zone) {
    uint16_t cx = x;
    while (*p && cx < limit) {
        size_t n = 0;
        uint8_t w = glyph_cols(p, &n);
        if (!w)
            w = 1; // a stray continuation byte is drawn as itself, not skipped
        if (n > 4)
            break;
        char g[5] = {0};
        for (size_t i = 0; i < n; i++)
            g[i] = p[i];
        re_screen_put(s, y, cx, g, style, zone);
        p += n;
        cx = (uint16_t)(cx + w);
    }
    return cx;
}

uint8_t re_screen_zone_at(const re_screen_t *s, uint16_t y, uint16_t x) {
    if (!re_screen_inside(s, y, x))
        return RE_SCREEN_ZONE_NONE;
    return s->cell[(size_t)y * s->cols + x].zone;
}

void re_screen_clear(re_screen_t *s) {
    if (!s->cell)
        return;
    // Every field, not just the glyph. A cell left with the width of the glyph that
    // used to be there is indistinguishable from the right half of a double width
    // glyph, and a renderer that skips those writes nothing at all for a cleared row.
    for (size_t i = 0, n = (size_t)s->rows * s->cols; i < n; i++) {
        s->cell[i].g[0] = '\0';
        s->cell[i].cols = 1;
        s->cell[i].style = (uint8_t)RE_ST_NONE;
        s->cell[i].zone = RE_SCREEN_ZONE_NONE;
    }
    // A clear is a new frame. Zone ids are drawing order within that frame, so the
    // counter has to start over with the cells. Left running, it walks up to the
    // ceiling and then every control is drawn with no zone, which is a click that
    // cannot land on anything.
    s->zone_count = 0;
}

uint8_t re_screen_zone(re_screen_t *s) {
    if (s->zone_count >= ZONE_MAX)
        return RE_SCREEN_ZONE_NONE;
    return ++s->zone_count;
}

void re_screen_put(re_screen_t *s, uint16_t y, uint16_t x, const char *glyph, uint8_t style,
                   uint8_t zone) {
    if (!re_screen_inside(s, y, x) || !glyph)
        return;
    size_t n = 0;
    uint8_t w = glyph_cols(glyph, &n);
    if (!w || n > 4)
        return;
    re_cell_t *c = at(s, y, x);
    for (size_t i = 0; i < n && i < 4; i++)
        c->g[i] = glyph[i];
    c->g[n] = '\0';
    c->cols = w;
    c->style = style;
    c->zone = zone;
    // A double width glyph claims the next column, so whatever was there has to be
    // overwritten as a continuation. Leaving it would let a stale glyph show through
    // on the right half of the character.
    if (w == 2 && re_screen_inside(s, y, x + 1)) {
        re_cell_t *c2 = at(s, y, x + 1);
        c2->g[0] = '\0';
        c2->cols = 0;
        c2->style = style;
        c2->zone = zone;
    }
}

static void blank(re_screen_t *s, uint16_t y, uint16_t x, uint8_t style, uint8_t zone) {
    re_cell_t *c = at(s, y, x);
    c->g[0] = '\0';
    c->cols = 1;
    c->style = style;
    c->zone = zone;
}

void re_screen_fill(re_screen_t *s, uint16_t y, uint16_t x, uint16_t w, uint16_t h,
                    const char *glyph, uint8_t style, uint8_t zone) {
    for (uint16_t r = 0; r < h; r++)
        for (uint16_t c = 0; c < w; c++) {
            uint16_t yy = (uint16_t)(y + r), xx = (uint16_t)(x + c);
            if (!re_screen_inside(s, yy, xx))
                continue;
            if (glyph)
                re_screen_put(s, yy, xx, glyph, style, zone);
            else
                blank(s, yy, xx, style, zone);
        }
}

void re_screen_hline(re_screen_t *s, uint16_t y, uint16_t x, uint16_t w, const char *glyph,
                     uint8_t style) {
    re_screen_fill(s, y, x, w, 1, glyph, style, RE_SCREEN_ZONE_NONE);
}

void re_screen_vline(re_screen_t *s, uint16_t y, uint16_t x, uint16_t h, const char *glyph,
                     uint8_t style) {
    re_screen_fill(s, y, x, 1, h, glyph, style, RE_SCREEN_ZONE_NONE);
}

void re_screen_box(re_screen_t *s, uint16_t y, uint16_t x, uint16_t w, uint16_t h,
                   const char *title, uint8_t style) {
    if (w < 2 || h < 2)
        return;
    const char *h_g = "\xe2\x94\x80", *v_g = "\xe2\x94\x82", *tl = "\xe2\x94\x8c",
               *tr = "\xe2\x94\x90", *bl = "\xe2\x94\x94", *br = "\xe2\x94\x98";
    if (!s->tui.unicode) {
        h_g = "-";
        v_g = "|";
        tl = tr = bl = br = "+";
    }
    re_screen_put(s, y, x, tl, style, RE_SCREEN_ZONE_NONE);
    re_screen_put(s, y, (uint16_t)(x + w - 1), tr, style, RE_SCREEN_ZONE_NONE);
    re_screen_put(s, (uint16_t)(y + h - 1), x, bl, style, RE_SCREEN_ZONE_NONE);
    re_screen_put(s, (uint16_t)(y + h - 1), (uint16_t)(x + w - 1), br, style, RE_SCREEN_ZONE_NONE);
    re_screen_hline(s, y, (uint16_t)(x + 1), (uint16_t)(w - 2), h_g, style);
    re_screen_hline(s, (uint16_t)(y + h - 1), (uint16_t)(x + 1), (uint16_t)(w - 2), h_g, style);
    re_screen_vline(s, (uint16_t)(y + 1), x, (uint16_t)(h - 2), v_g, style);
    re_screen_vline(s, (uint16_t)(y + 1), (uint16_t)(x + w - 1), (uint16_t)(h - 2), v_g, style);
    if (title && *title) {
        // The title sits one space in from the corner so the frame reads as a frame.
        // Walked by sequence rather than by byte: splitting a two byte glyph across
        // two cells would draw two replacement characters and cost two columns.
        uint16_t tx = (uint16_t)(x + 2);
        uint16_t edge = (uint16_t)(x + w - 2);
        for (const char *p = title; *p && tx < edge;) {
            size_t n = 0;
            glyph_cols(p, &n);
            if (n > 4)
                break;
            char g[5] = {0};
            for (size_t i = 0; i < n; i++)
                g[i] = p[i];
            re_screen_put(s, y, tx, g, (uint8_t)RE_ST_TITLE, RE_SCREEN_ZONE_NONE);
            p += n;
            tx += 1;
        }
    }
}
