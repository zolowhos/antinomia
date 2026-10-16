// re_ui.h - hover and click bindings over screen zones.
// Module: util (C11).
// Owns: the hot zone, and the click and hover handlers a zone carries.
// Depends: re_screen.h. No terminal, no decoder. The caller names the cell.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "utils/tui/re_screen.h"

// How many controls one screen binds. Past this a bind fails rather than growing,
// because a frame that needs more has already stopped being readable.
#define RE_UI_MAX 64u

// The three things a pointer does. A click is a down and an up on the same zone,
// so a drag that leaves the control does not activate it.
typedef enum {
    RE_UI_MOVE = 0,
    RE_UI_DOWN,
    RE_UI_UP,
} re_ui_ptr_t;

typedef void (*re_ui_click_fn)(void *user);
// over is true when the pointer enters the zone and false when it leaves.
typedef void (*re_ui_hover_fn)(void *user, bool over);

typedef struct {
    uint8_t zone;
    re_ui_click_fn on_click;
    re_ui_hover_fn on_hover;
    void *user; // caller owned. This layer never frees it.
} re_ui_slot_t;

typedef struct {
    re_ui_slot_t slot[RE_UI_MAX];
    size_t n;
    uint8_t hot;     // zone under the pointer, or RE_SCREEN_ZONE_NONE
    uint8_t pressed; // zone the button went down on
} re_ui_t;

void re_ui_init(re_ui_t *u);

// Drop the bindings. Hot and pressed stay: the release of a click arrives on the
// next frame, and wiping either would swallow the click or blink the highlight.
void re_ui_clear(re_ui_t *u);

// Bind one zone. False when the zone is none or the table is full. Binding the
// same zone again replaces the handlers, so a redraw can restate them.
bool re_ui_bind(re_ui_t *u, uint8_t zone, re_ui_click_fn on_click, re_ui_hover_fn on_hover,
                void *user);

// Move, press, or release. Hover runs on enter and on leave. Click runs on release
// only when that release is on the zone the press started on.
void re_ui_pointer(re_ui_t *u, const re_screen_t *s, uint16_t row, uint16_t col, re_ui_ptr_t how);

// Run the click handler. False when the zone has none. Enter uses this, so the
// keyboard and the pointer share one binding.
bool re_ui_click(re_ui_t *u, uint8_t zone);

// Paint the hot zone in the hover style and the held zone in the press style.
// The held zone wins when they are the same, so a click stays dark blue instead of
// flipping back to the hover color. Widgets draw without knowing the pointer.
void re_ui_mark(const re_ui_t *u, re_screen_t *s, uint8_t hover, uint8_t press);

#ifdef __cplusplus
}
#endif
