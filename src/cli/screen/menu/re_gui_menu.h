// re_gui_menu.h - antistrefo-style menu bar, action buttons, and dropdowns.
// Module: cli (C11).
// Owns: File/Edit/Jump/Search/Options/Help chrome and Run/Step button row.
// Depends: re_screen, re_ui, re_keys. Debug panes stay in re_cpu_paint.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "cli/screen/keys/re_keys.h"
#include "utils/tui/re_screen.h"
#include "utils/tui/re_ui.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Top chrome: menu row plus action-button row. Panes begin below this.
#define RE_GUI_CHROME_TOP 2u

typedef struct {
    size_t *focus; // pane index (re_cpu_pane_t)
    size_t n_panes;
    bool *repaint;
    bool *quit;
    bool *color;
    int *open;   // -1 closed, else toolbar index
    uint8_t bar; // zone of File (first toolbar control)
    uint8_t *zone;
    uint8_t *nitems;
    re_act_t *pending; // button/menu item sets this; gui runs it
    void (*log_fn)(void *user, const char *msg);
    void *log_user;
    const char *path; // drawn on the menu row
    const char *mark; // product mark, e.g. "antinomia 0.1.0"
    uint8_t *btn_zone;
    uint8_t *btn_n;
} re_gui_menu_ctx_t;

// Draw menu row (bracket toolbar) and the action button row under it.
void re_gui_chrome_draw(re_screen_t *s, re_gui_menu_ctx_t *c);

// Draw the open dropdown under its toolbar title. No-op when closed.
void re_gui_menu_draw(re_screen_t *s, re_gui_menu_ctx_t *c);

// Bind toolbar titles, dropdown rows, and action buttons into the UI table.
void re_gui_menu_bind(re_ui_t *u, re_gui_menu_ctx_t *c);

#ifdef __cplusplus
}
#endif
