// re_cfg.h - shared UX config paths and SGR/ini helpers for themes and keys.
// Module: util (C11).
// Owns: config file discovery, style-name lookup, and SGR escape builders.
// Depends: re_arena, re_str, re_buf. No TUI draw; callers apply results.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

#include "utils/tui/re_tui.h"

#define RE_CFG_PATH_MAX 512u
#define RE_CFG_SGR_MAX 64u

typedef enum {
    RE_CFG_KIND_NONE = 0,
    RE_CFG_KIND_JSON,
    RE_CFG_KIND_INI,
} re_cfg_kind_t;

/* Resolve a config file: env override, cwd, then XDG/APPDIR antinomia/. */
bool re_cfg_find(const char *env_name, const char *base_json, const char *base_ini, char *dst,
                 size_t cap);

re_cfg_kind_t re_cfg_sniff(const char *text, size_t n);
re_cfg_kind_t re_cfg_kind_path(const char *path);

/* Map a style slot name ("accent", "title", ...) to re_style_t, or -1. */
int re_cfg_style_id(const char *name, size_t n);

/* Build "\x1b[<sgr>m" into dst (NUL terminated). False when sgr is empty/too long. */
bool re_cfg_sgr_esc(char *dst, size_t cap, const char *sgr, size_t n);

/* Copy NUL-terminated src into dst with cap, always terminating. */
void re_cfg_copy(char *dst, size_t cap, const char *src, size_t n);

#ifdef __cplusplus
}
#endif
