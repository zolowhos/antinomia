// re_ui.c - hover and click dispatch over a screen's zones.
// Module: util (C11).
// Owns: re_ui.h. Reads a grid, calls the handlers the caller bound.
// Depends: re_ui.h, re_screen.h. No terminal, no allocation, no globals.
#include "utils/tui/re_ui.h"

#include <string.h>

void re_ui_init(re_ui_t *u) {
    if (u)
        memset(u, 0, sizeof(*u));
}

void re_ui_clear(re_ui_t *u) {
    if (!u)
        return;
    // Bindings only. A press and its release arrive on different frames, and the
    // frame between them rebinds. Wiping pressed here would swallow every click.
    u->n = 0;
}

static re_ui_slot_t *find_slot(re_ui_t *u, uint8_t zone) {
    if (zone == RE_SCREEN_ZONE_NONE)
        return NULL;
    for (size_t i = 0; i < u->n; i++)
        if (u->slot[i].zone == zone)
            return &u->slot[i];
    return NULL;
}

bool re_ui_bind(re_ui_t *u, uint8_t zone, re_ui_click_fn on_click, re_ui_hover_fn on_hover,
                void *user) {
    if (!u || zone == RE_SCREEN_ZONE_NONE)
        return false;
    re_ui_slot_t *have = find_slot(u, zone);
    if (!have) {
        if (u->n == RE_UI_MAX)
            return false;
        have = &u->slot[u->n++];
    }
    have->zone = zone;
    have->on_click = on_click;
    have->on_hover = on_hover;
    have->user = user;
    return true;
}

bool re_ui_click(re_ui_t *u, uint8_t zone) {
    re_ui_slot_t *slot = u ? find_slot(u, zone) : NULL;
    if (!slot || !slot->on_click)
        return false;
    slot->on_click(slot->user);
    return true;
}

static void set_hot(re_ui_t *u, uint8_t zone) {
    if (zone == u->hot)
        return;
    re_ui_slot_t *old = find_slot(u, u->hot);
    re_ui_slot_t *now = find_slot(u, zone);
    u->hot = zone;
    if (old && old->on_hover)
        old->on_hover(old->user, false);
    if (now && now->on_hover)
        now->on_hover(now->user, true);
}

static uint8_t cell_zone(const re_screen_t *s, uint16_t row, uint16_t col) {
    if (!s)
        return RE_SCREEN_ZONE_NONE;
    return re_screen_zone_at(s, row, col);
}

void re_ui_pointer(re_ui_t *u, const re_screen_t *s, uint16_t row, uint16_t col, re_ui_ptr_t how) {
    if (!u)
        return;
    uint8_t zone = cell_zone(s, row, col);
    if (how == RE_UI_MOVE) {
        set_hot(u, zone);
        return;
    }
    if (how == RE_UI_DOWN) {
        set_hot(u, zone);
        u->pressed = zone;
        return;
    }
    set_hot(u, zone);
    if (zone != RE_SCREEN_ZONE_NONE && zone == u->pressed)
        (void)re_ui_click(u, zone);
    u->pressed = RE_SCREEN_ZONE_NONE;
}

static void paint_zone(re_screen_t *s, uint8_t zone, uint8_t style) {
    if (zone == RE_SCREEN_ZONE_NONE)
        return;
    size_t n = (size_t)s->rows * s->cols;
    for (size_t i = 0; i < n; i++)
        if (s->cell[i].zone == zone)
            s->cell[i].style = style;
}

void re_ui_mark(const re_ui_t *u, re_screen_t *s, uint8_t hover, uint8_t press) {
    if (!u || !s || !s->cell)
        return;
    // Pressed last, so a held button is dark even while the pointer is still on it.
    if (u->hot != u->pressed)
        paint_zone(s, u->hot, hover);
    paint_zone(s, u->pressed, press);
}
