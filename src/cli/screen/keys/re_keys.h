// re_keys.h - configurable TUI keybindings with x64dbg-ish defaults.
// Module: cli (C11).
// Owns: action enum, default map, ini/json load, and event match.
// Depends: re_input, re_cfg. Defaults avoid fighting Tab/arrows/Ctrl-C.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "utils/cfg/re_cfg.h"
#include "cli/screen/re_input.h"

typedef enum {
    RE_ACT_NONE = 0,
    RE_ACT_QUIT,
    RE_ACT_FOCUS_NEXT,
    RE_ACT_FOCUS_PREV,
    RE_ACT_RUN,
    RE_ACT_STEP_INTO,
    RE_ACT_STEP_OVER,
    RE_ACT_PAUSE,
    RE_ACT_TOGGLE_BP,
    RE_ACT_COUNT
} re_act_t;

#define RE_KEYS_MAX 48u

typedef struct {
    uint32_t key;
    bool ctrl;
    bool alt;
    bool shift;
    re_act_t act;
} re_keybind_t;

typedef struct {
    re_keybind_t binds[RE_KEYS_MAX];
    size_t n;
} re_keys_t;

void re_keys_defaults(re_keys_t *k);
bool re_keys_load_path(re_keys_t *k, const char *path);
bool re_keys_load_text(re_keys_t *k, const char *text, size_t n, re_cfg_kind_t kind);
re_act_t re_keys_match(const re_keys_t *k, const re_ev_t *ev);
const char *re_keys_act_name(re_act_t act);

/* Resolve ANTINOMIA_KEYS / keys.json|ini. True if a file loaded over defaults. */
bool re_keys_autoload(re_keys_t *k);

#ifdef __cplusplus
}
#endif
