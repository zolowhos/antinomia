// re_theme.h - loadable ANSI style themes for the TUI (ini or json).
// Module: util (C11).
// Owns: theme defaults, file load, and apply into re_tui style slots.
// Depends: re_cfg, re_tui, re_buf, re_jr. Values are SGR parameter strings.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

#include "utils/cfg/re_cfg.h"
#include "utils/tui/re_tui.h"

#define RE_THEME_NAME_MAX 32u

typedef struct {
    char name[RE_THEME_NAME_MAX];
    char on[RE_ST_CALL + 1][RE_CFG_SGR_MAX];
    char off[RE_ST_CALL + 1][RE_CFG_SGR_MAX];
} re_theme_t;

void re_theme_defaults(re_theme_t *t);
bool re_theme_load_path(re_theme_t *t, const char *path);
bool re_theme_load_text(re_theme_t *t, const char *text, size_t n, re_cfg_kind_t kind);
void re_theme_apply(const re_theme_t *t);

/* Resolve ANTINOMIA_THEME / theme.json|ini and load+apply. True if a file loaded. */
bool re_theme_autoload(re_theme_t *t);

#ifdef __cplusplus
}
#endif
