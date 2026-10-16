// re_draw.h - repainting only the cells that changed.
// Module: cli (C11).
// Owns: the previous frame, and the diff between it and the current one.
// Depends: re_screen.h, re_strbuf.h. Emits escape sequences but performs no I/O; the
//           caller decides what to do with the bytes.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "utils/tui/re_screen.h"

typedef struct {
    re_screen_t prev; // the last frame, same geometry as cur
    re_strbuf_t out;  // where the diff is written
    bool valid;       // false until the first full frame has been painted
    uint32_t cells;   // cells written by the last diff, for a status line
} re_draw_t;

bool re_draw_init(re_draw_t *d, re_arena_t *a, uint16_t rows, uint16_t cols);
void re_draw_free(re_draw_t *d);

// Reallocate at a new size. A resize invalidates the previous frame, so the next diff
// is a full repaint: that is correct rather than merely easy, because after a resize
// every cell is at a new place.
bool re_draw_resize(re_draw_t *d, re_arena_t *a, uint16_t rows, uint16_t cols);

// The diff from prev to cur, written into d->out, plus the cursor moves and the
// clear-screen and cursor-hide that belong to the first paint. After this the previous
// frame is cur, so a second call with an unchanged cur writes nothing at all.
void re_draw_frame(re_draw_t *d, const re_screen_t *cur);

// The full repaint, for after a resize or when the terminal has been scribbled on.
// Equivalent to a diff against an empty frame, which is why it is the same code path.
void re_draw_full(re_draw_t *d, const re_screen_t *cur);

// Where the cursor should be left after the frame. A terminal left with the cursor
// somewhere it should not be will happily draw the next frame's first cell over it.
void re_draw_home(re_draw_t *d, uint16_t row, uint16_t col);

#ifdef __cplusplus
}
#endif
