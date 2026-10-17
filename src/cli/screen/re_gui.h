// re_gui.h - the full screen debugger front end with antistrefo chrome.
// Module: cli (C11).
// Owns: the interactive CPU TUI loop entry (menus, buttons, panes).
// Depends: re_term, re_input, re_focus, re_draw, re_screen, re_gui_menu.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "cli/app/re_table.h"

int re_cmd_gui(re_ctx_t *ctx, const char *path, int argc, char **argv);
bool re_gui_wants_file(const char *arg);

#ifdef __cplusplus
}
#endif
