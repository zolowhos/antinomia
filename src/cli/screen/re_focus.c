// re_focus.c - the tab order over zones and the click path.
// Module: cli (C11).
// Owns: re_focus.h. Reads a grid, changes a cursor.
// Depends: re_focus.h, re_screen.h. No terminal, no I/O.
#include "cli/screen/re_focus.h"

void re_focus_init(re_focus_t *f) {
    f->n = 0;
    f->at = 0;
    f->zone = RE_SCREEN_ZONE_NONE;
    f->wrapped = false;
    f->truncated = false;
}

void re_focus_build(re_focus_t *f, const re_screen_t *s) {
    // The cursor survives the rebuild by id rather than by position, so a redraw that
    // adds a control above the focused one does not silently move the focus.
    uint8_t keep = f->zone;
    f->n = 0;
    f->truncated = false;
    if (!s->cell) {
        f->at = 0;
        f->zone = RE_SCREEN_ZONE_NONE;
        return;
    }
    // Ascending id is drawing order, which is the order a person meets them in. A
    // counting sort over the id space would be faster and would produce a tab order
    // that jumped around, so the grid is swept once and each first sighting appended.
    for (uint16_t y = 0; y < s->rows; y++) {
        for (uint16_t x = 0; x < s->cols; x++) {
            uint8_t z = s->cell[(size_t)y * s->cols + x].zone;
            if (z == RE_SCREEN_ZONE_NONE)
                continue;
            bool seen = false;
            for (size_t i = 0; i < f->n; i++)
                if (f->order[i] == z) {
                    seen = true;
                    break;
                }
            if (seen)
                continue;
            if (f->n == RE_FOCUS_MAX) {
                f->truncated = true;
                // Stop scanning rather than carry on: a partial order that silently
                // skipped controls would make those controls unreachable by keyboard
                // while still looking reachable.
                f->at = 0;
                f->zone = f->n ? f->order[0] : RE_SCREEN_ZONE_NONE;
                return;
            }
            f->order[f->n++] = z;
        }
    }
    f->at = 0;
    if (re_focus_set(f, keep))
        return;
    f->zone = f->n ? f->order[0] : RE_SCREEN_ZONE_NONE;
}

bool re_focus_set(re_focus_t *f, uint8_t zone) {
    for (size_t i = 0; i < f->n; i++) {
        if (f->order[i] == zone) {
            f->at = i;
            f->zone = zone;
            return true;
        }
    }
    return false;
}

bool re_focus_next(re_focus_t *f) {
    if (!f->n)
        return false;
    f->at++;
    f->wrapped = false;
    if (f->at == f->n) {
        f->at = 0;
        f->wrapped = true;
    }
    f->zone = f->order[f->at];
    return true;
}

bool re_focus_prev(re_focus_t *f) {
    if (!f->n)
        return false;
    f->wrapped = false;
    if (f->at == 0) {
        f->at = f->n - 1;
        f->wrapped = true;
    } else {
        f->at--;
    }
    f->zone = f->order[f->at];
    return true;
}

uint8_t re_focus_hit(const re_screen_t *s, uint16_t row, uint16_t col) {
    return re_screen_zone_at(s, row, col);
}

bool re_focus_click(re_focus_t *f, const re_screen_t *s, uint16_t row, uint16_t col) {
    uint8_t z = re_focus_hit(s, row, col);
    if (z == RE_SCREEN_ZONE_NONE)
        return false;
    if (re_focus_set(f, z))
        return true;
    // The grid carries a zone the order does not, which happens when the layout drew
    // a control past the order's ceiling. It is not focusable by keyboard, so the
    // click is refused rather than half honoured.
    return false;
}
