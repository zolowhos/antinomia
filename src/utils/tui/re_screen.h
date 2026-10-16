// re_screen.h - a cell grid and the widgets a full screen report is composed of.
// Module: util (C11).
// Owns: the grid: cells, rectangles, and the widgets drawn into them. Never reads input.
// Depends: re_arena, re_strbuf, re_tui. Writes only into a caller's buffer.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "utils/mem/re_arena.h"
#include "utils/text/re_strbuf.h"
#include "utils/tui/re_tui.h"

// A full screen report is bounded by what a terminal can be, not by what a person
// might like. These are generous ceilings for the model; the caller passes the real
// size and anything larger is refused rather than clipped silently, because a layout
// computed for 200 columns and drawn into 80 is a report that is wrong, not short.
#define RE_SCREEN_MAX_ROWS 200u
#define RE_SCREEN_MAX_COLS 500u

// Zone 0 means the cell belongs to no control. Every other value is the id of the
// control the cell is part of, which is what makes a click resolvable to a widget
// without the widget having to know its own coordinates: the grid already recorded
// them at draw time.
#define RE_SCREEN_ZONE_NONE 0u

// One glyph. cols is the display width, so a two column glyph marks its second
// column with cols 0 and the row renderer skips it. That is what stops a CJK or box
// drawing glyph from shifting everything after it left by one column.
typedef struct {
    char g[5];     // the glyph, NUL terminated. An empty string is a blank cell.
    uint8_t cols;  // 0 continuation, 1 or 2 display columns
    uint8_t style; // re_style_t
    uint8_t zone;  // RE_SCREEN_ZONE_NONE, or the id of the owning control
} re_cell_t;

typedef struct {
    uint16_t rows;
    uint16_t cols;
    re_cell_t *cell; // rows * cols, from the arena
    re_tui_t tui;
    re_strbuf_t *out; // where re_screen_draw writes
    uint16_t cy;      // caret row, reported by the status line
    uint16_t cx;      // caret column
    uint8_t zone_count;
    bool overflow; // the requested size exceeded the ceilings
} re_screen_t;

// Allocate the grid and bind it to a destination. Returns false when rows or cols is
// zero or above the ceiling, in which case the screen is left blank and flagged so
// the caller can fall back rather than draw into a buffer that cannot hold it.
bool re_screen_init(re_screen_t *s, re_arena_t *a, re_strbuf_t *out, uint16_t rows, uint16_t cols);

void re_screen_clear(re_screen_t *s);

// Whether a cell exists. Shared by every widget, because a widget that clips against
// its own idea of the grid is a widget that draws outside it.
bool re_screen_inside(const re_screen_t *s, uint16_t y, uint16_t x);

// The control at a cell, or RE_SCREEN_ZONE_NONE. The other half of the zone contract:
// the id answers what was drawn, this answers what was clicked.
uint8_t re_screen_zone_at(const re_screen_t *s, uint16_t y, uint16_t x);

// A new clickable control id, or RE_SCREEN_ZONE_NONE past the ceiling. Ids are
// handed out in drawing order and are stable for a given layout, which is what lets
// a click be resolved by walking the layout rather than by guessing from coordinates.
uint8_t re_screen_zone(re_screen_t *s);

// Primitives. All of them clip against the grid and return silently when the
// rectangle falls outside it, because a layout that hands out a negative width is a
// layout bug and should not also be a crash.
void re_screen_put(re_screen_t *s, uint16_t y, uint16_t x, const char *glyph, uint8_t style,
                   uint8_t zone);

// A whole string rather than one glyph, which is what every caller holding text
// actually wants. Returns the column just past what was written, so a caller can
// tell whether anything fitted instead of assuming it did.
uint16_t re_screen_put_run(re_screen_t *s, uint16_t y, uint16_t x, uint16_t limit, const char *p,
                           uint8_t style, uint8_t zone);

void re_screen_fill(re_screen_t *s, uint16_t y, uint16_t x, uint16_t w, uint16_t h,
                    const char *glyph, uint8_t style, uint8_t zone);
void re_screen_hline(re_screen_t *s, uint16_t y, uint16_t x, uint16_t w, const char *glyph,
                     uint8_t style);
void re_screen_vline(re_screen_t *s, uint16_t y, uint16_t x, uint16_t h, const char *glyph,
                     uint8_t style);

// A framed box with a title in its top edge. An empty title leaves the edge solid.
void re_screen_box(re_screen_t *s, uint16_t y, uint16_t x, uint16_t w, uint16_t h,
                   const char *title, uint8_t style);

// Widgets. Each takes a rectangle and clips to it, so a caller can hand over the
// space it has and let the widget use as much as fits.
void re_screen_tabs(re_screen_t *s, uint16_t y, uint16_t x, uint16_t w, const char *const *labels,
                    size_t n, size_t active);
void re_screen_legend(re_screen_t *s, uint16_t y, uint16_t x, uint16_t w, const char *const *labels,
                      size_t n);
void re_screen_toolbar(re_screen_t *s, uint16_t y, const char *const *items, size_t n);
void re_screen_nav(re_screen_t *s, uint16_t y, uint16_t x, uint16_t w, size_t pos, size_t total);
void re_screen_gutter(re_screen_t *s, uint16_t y, uint16_t x, uint16_t h, uint32_t first,
                      const uint8_t *marks, size_t n);
void re_screen_vscroll(re_screen_t *s, uint16_t y, uint16_t x, uint16_t h, uint16_t pos,
                       uint16_t total);
void re_screen_status(re_screen_t *s, uint16_t y, uint16_t x, uint16_t w, const char *left,
                      const char *right);
void re_screen_list(re_screen_t *s, uint16_t y, uint16_t x, uint16_t w, uint16_t h,
                    const char *head, const char *const *rows, size_t n, size_t sel);
void re_screen_code(re_screen_t *s, uint16_t y, uint16_t x, uint16_t w, uint16_t h,
                    const char *const *lines, size_t n, uint32_t base, const uint8_t *marks);

// Blit the grid into the output buffer, moving the cursor for each row rather than
// emitting a newline, so the result is positionable and a redraw can replace one
// region without repainting the screen.
void re_screen_draw(const re_screen_t *s);

// Render the grid as plain text rows with no escapes and no cursor moves. This is
// what the tests assert on, and what a reader gets when colour is turned off: the
// layout is the same shape either way, which is the point of separating them.
void re_screen_dump(const re_screen_t *s, re_strbuf_t *dst);
#ifdef __cplusplus
}
#endif
