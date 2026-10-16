// re_focus.h - moving between the clickable regions of a grid, by keyboard or by click.
// Module: cli (C11).
// Owns: the tab order over zones, and resolving a click to a zone.
// Depends: re_screen.h. Touches no terminal and holds no file open.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "utils/tui/re_screen.h"

// As many controls as a grid can plausibly carry. Past this the tab order stops being
// something a person can hold in their head, and a limit is the honest response: the
// order stays complete for everything it does cover, and reports when it is not.
#define RE_FOCUS_MAX 256u

typedef struct {
    uint8_t order[RE_FOCUS_MAX]; // zone ids, ascending, which is drawing order
    size_t n;
    size_t at;      // where the cursor is in that order
    uint8_t zone;   // the focused id, 0 for none
    bool wrapped;   // the last move ran off an end and came back
    bool truncated; // the grid held more zones than the order can carry
} re_focus_t;

void re_focus_init(re_focus_t *f);

// Read the tab order out of a finished grid. Must run after the layout is composed and
// before the first draw, because a zone that is drawn later has a higher id and belongs
// at the end of the order. Rebuilding on every frame is cheap and is what keeps the
// order honest when the layout changes shape.
void re_focus_build(re_focus_t *f, const re_screen_t *s);

// The next or previous control, wrapping at both ends. Wrapping rather than stopping is
// deliberate: a Tab that stops at the end looks like the terminal has lost the key.
bool re_focus_next(re_focus_t *f);
bool re_focus_prev(re_focus_t *f);

// Put the cursor on one control by id. False when the id is not in the order.
bool re_focus_set(re_focus_t *f, uint8_t zone);

// The control under a cell, which is the id the grid recorded there. RE_SCREEN_ZONE_NONE
// when the cell belongs to no control. This is the whole click path: the terminal gives
// coordinates, the grid answers, and nothing has to know the layout.
uint8_t re_focus_hit(const re_screen_t *s, uint16_t row, uint16_t col);

// Move the cursor to whatever was clicked, and report whether that was a control at
// all. A click on empty space is not an error and must not move the focus, or every
// stray click in the body would throw the keyboard cursor away.
bool re_focus_click(re_focus_t *f, const re_screen_t *s, uint16_t row, uint16_t col);

#ifdef __cplusplus
}
#endif
