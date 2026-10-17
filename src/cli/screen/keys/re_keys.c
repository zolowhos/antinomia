// re_keys.c - x64dbg-ish defaults and ini/json keybinding load.
// Module: cli (C11).
// Owns: re_keys.h. F-keys for debug; Tab/Esc/q stay TUI-native.
// Depends: re_keys.h, re_jr, re_buf, re_str.
#include "cli/screen/keys/re_keys.h"

#include "utils/json/re_jr.h"
#include "utils/mem/re_arena.h"
#include "utils/mem/re_buf.h"
#include "utils/text/re_str.h"

#include <string.h>

static void add_bind(re_keys_t *k, uint32_t key, bool ctrl, bool alt, bool shift, re_act_t act) {
    if (!k || k->n >= RE_KEYS_MAX || act == RE_ACT_NONE)
        return;
    k->binds[k->n].key = key;
    k->binds[k->n].ctrl = ctrl;
    k->binds[k->n].alt = alt;
    k->binds[k->n].shift = shift;
    k->binds[k->n].act = act;
    k->n++;
}

void re_keys_defaults(re_keys_t *k) {
    if (!k)
        return;
    memset(k, 0, sizeof(*k));
    /* TUI chrome: keep these so the view stays usable without a config file. */
    add_bind(k, 'q', false, false, false, RE_ACT_QUIT);
    add_bind(k, 'Q', false, false, false, RE_ACT_QUIT);
    add_bind(k, RE_KEY_ESCAPE, false, false, false, RE_ACT_QUIT);
    add_bind(k, RE_KEY_TAB, false, false, false, RE_ACT_FOCUS_NEXT);
    add_bind(k, RE_KEY_STAB, false, false, false, RE_ACT_FOCUS_PREV);
    /* x64dbg-ish debug keys that do not steal arrows or Ctrl-C. */
    add_bind(k, RE_KEY_F2, false, false, false, RE_ACT_TOGGLE_BP);
    add_bind(k, RE_KEY_F5, false, false, false, RE_ACT_RUN);
    add_bind(k, RE_KEY_F7, false, false, false, RE_ACT_STEP_INTO);
    add_bind(k, RE_KEY_F8, false, false, false, RE_ACT_STEP_OVER);
    add_bind(k, RE_KEY_F9, false, false, false, RE_ACT_RUN);
    add_bind(k, RE_KEY_F12, false, false, false, RE_ACT_PAUSE);
}

const char *re_keys_act_name(re_act_t act) {
    switch (act) {
        case RE_ACT_QUIT:
            return "quit";
        case RE_ACT_FOCUS_NEXT:
            return "focus_next";
        case RE_ACT_FOCUS_PREV:
            return "focus_prev";
        case RE_ACT_RUN:
            return "run";
        case RE_ACT_STEP_INTO:
            return "step_into";
        case RE_ACT_STEP_OVER:
            return "step_over";
        case RE_ACT_PAUSE:
            return "pause";
        case RE_ACT_TOGGLE_BP:
            return "toggle_bp";
        default:
            return "none";
    }
}

static re_act_t act_from_name(re_str_t s) {
    if (re_str_ieq_cstr(s, "quit"))
        return RE_ACT_QUIT;
    if (re_str_ieq_cstr(s, "focus_next") || re_str_ieq_cstr(s, "focus-next"))
        return RE_ACT_FOCUS_NEXT;
    if (re_str_ieq_cstr(s, "focus_prev") || re_str_ieq_cstr(s, "focus-prev"))
        return RE_ACT_FOCUS_PREV;
    if (re_str_ieq_cstr(s, "run") || re_str_ieq_cstr(s, "continue") || re_str_ieq_cstr(s, "g"))
        return RE_ACT_RUN;
    if (re_str_ieq_cstr(s, "step_into") || re_str_ieq_cstr(s, "step-into") ||
        re_str_ieq_cstr(s, "sti") || re_str_ieq_cstr(s, "t"))
        return RE_ACT_STEP_INTO;
    if (re_str_ieq_cstr(s, "step_over") || re_str_ieq_cstr(s, "step-over") ||
        re_str_ieq_cstr(s, "sto") || re_str_ieq_cstr(s, "p"))
        return RE_ACT_STEP_OVER;
    if (re_str_ieq_cstr(s, "pause"))
        return RE_ACT_PAUSE;
    if (re_str_ieq_cstr(s, "toggle_bp") || re_str_ieq_cstr(s, "toggle-bp") ||
        re_str_ieq_cstr(s, "bp"))
        return RE_ACT_TOGGLE_BP;
    return RE_ACT_NONE;
}

static bool parse_key_token(re_str_t tok, uint32_t *key, bool *ctrl, bool *alt, bool *shift) {
    re_str_t rest = tok;
    *ctrl = *alt = *shift = false;
    for (;;) {
        long plus = re_str_find_cstr(rest, "+", 0);
        re_str_t part = plus < 0 ? rest : re_strn(rest.p, (size_t)plus);
        if (plus >= 0)
            rest = re_strn(rest.p + (size_t)plus + 1u, rest.n - (size_t)plus - 1u);
        if (re_str_ieq_cstr(part, "ctrl") || re_str_ieq_cstr(part, "c")) {
            *ctrl = true;
            if (plus < 0)
                return false;
            continue;
        }
        if (re_str_ieq_cstr(part, "alt") || re_str_ieq_cstr(part, "a")) {
            *alt = true;
            if (plus < 0)
                return false;
            continue;
        }
        if (re_str_ieq_cstr(part, "shift") || re_str_ieq_cstr(part, "s")) {
            *shift = true;
            if (plus < 0)
                return false;
            continue;
        }
        /* Final key token. */
        if (part.n == 1 && (unsigned char)part.p[0] < 0x80u) {
            *key = (uint32_t)(unsigned char)part.p[0];
            return true;
        }
        if (re_str_ieq_cstr(part, "esc") || re_str_ieq_cstr(part, "escape")) {
            *key = RE_KEY_ESCAPE;
            return true;
        }
        if (re_str_ieq_cstr(part, "tab")) {
            *key = RE_KEY_TAB;
            return true;
        }
        if (re_str_ieq_cstr(part, "s-tab") || re_str_ieq_cstr(part, "stab") ||
            re_str_ieq_cstr(part, "backtab")) {
            *key = RE_KEY_STAB;
            return true;
        }
        if (part.n >= 2 && (part.p[0] == 'f' || part.p[0] == 'F') && part.p[1] >= '1' &&
            part.p[1] <= '9') {
            unsigned fn = (unsigned)(part.p[1] - '0');
            if (part.n == 3 && part.p[2] >= '0' && part.p[2] <= '2')
                fn = fn * 10u + (unsigned)(part.p[2] - '0');
            else if (part.n != 2)
                return false;
            if (fn < 1u || fn > 12u)
                return false;
            *key = RE_KEY_F1 + (fn - 1u);
            return true;
        }
        return false;
    }
}

static void bind_named(re_keys_t *k, re_str_t keytok, re_str_t actname) {
    uint32_t key = 0;
    bool ctrl = false, alt = false, shift = false;
    re_act_t act = act_from_name(actname);
    if (act == RE_ACT_NONE)
        return;
    if (!parse_key_token(keytok, &key, &ctrl, &alt, &shift))
        return;
    add_bind(k, key, ctrl, alt, shift, act);
}

re_act_t re_keys_match(const re_keys_t *k, const re_ev_t *ev) {
    size_t i;
    if (!k || !ev || ev->kind != RE_EV_KEY)
        return RE_ACT_NONE;
    for (i = 0; i < k->n; i++) {
        const re_keybind_t *b = &k->binds[i];
        if (b->key == ev->key && b->ctrl == ev->ctrl && b->alt == ev->alt && b->shift == ev->shift)
            return b->act;
    }
    return RE_ACT_NONE;
}

static re_str_t trim_span(const char *p, size_t n) {
    while (n && (*p == ' ' || *p == '\t')) {
        p++;
        n--;
    }
    while (n && (p[n - 1] == ' ' || p[n - 1] == '\t' || p[n - 1] == '\r'))
        n--;
    return re_strn(p, n);
}

static bool load_json(re_keys_t *k, const char *text, size_t n) {
    re_arena_t a;
    re_jr_t root;
    const re_jr_t *arr;
    size_t i;
    re_arena_init(&a, 64u * 1024u);
    if (!re_jr_parse(&a, text, n, &root) || root.kind != RE_JR_OBJ) {
        re_arena_free(&a);
        return false;
    }
    arr = re_jr_get(&root, "bindings");
    if (!arr || arr->kind != RE_JR_ARR) {
        re_arena_free(&a);
        return false;
    }
    k->n = 0;
    for (i = 0; i < arr->count; i++) {
        const re_jr_t *item = &arr->items[i];
        const re_jr_t *jk;
        const re_jr_t *ja;
        if (item->kind != RE_JR_OBJ)
            continue;
        jk = re_jr_get(item, "key");
        ja = re_jr_get(item, "action");
        if (!jk || jk->kind != RE_JR_STR || !ja || ja->kind != RE_JR_STR)
            continue;
        bind_named(k, jk->str, ja->str);
    }
    re_arena_free(&a);
    return k->n > 0;
}

static void ini_line(re_keys_t *k, re_str_t line, int *in_keys) {
    long eq;
    if (line.n == 0 || line.p[0] == '#' || line.p[0] == ';')
        return;
    if (line.p[0] == '[') {
        *in_keys = re_str_istarts_cstr(line, "[keys]") ? 1 : 0;
        return;
    }
    if (!*in_keys)
        return;
    eq = re_str_find_cstr(line, "=", 0);
    if (eq < 0)
        return;
    bind_named(k, trim_span(line.p, (size_t)eq),
               trim_span(line.p + (size_t)eq + 1u, line.n - (size_t)eq - 1u));
}

static bool load_ini(re_keys_t *k, const char *text, size_t n) {
    size_t i = 0;
    int in_keys = 0;
    k->n = 0;
    while (i < n) {
        size_t start = i;
        while (i < n && text[i] != '\n')
            i++;
        ini_line(k, trim_span(text + start, i - start), &in_keys);
        if (i < n)
            i++;
    }
    return k->n > 0;
}

bool re_keys_load_text(re_keys_t *k, const char *text, size_t n, re_cfg_kind_t kind) {
    if (!k || !text)
        return false;
    if (kind == RE_CFG_KIND_NONE)
        kind = re_cfg_sniff(text, n);
    if (kind == RE_CFG_KIND_JSON)
        return load_json(k, text, n);
    if (kind == RE_CFG_KIND_INI)
        return load_ini(k, text, n);
    return false;
}

bool re_keys_load_path(re_keys_t *k, const char *path) {
    re_arena_t a;
    re_file_t f;
    re_cfg_kind_t kind;
    bool ok;
    if (!k || !path)
        return false;
    re_arena_init(&a, 4u * 1024u);
    if (re_file_open(path, &a, &f) != RE_OK) {
        re_arena_free(&a);
        return false;
    }
    kind = re_cfg_kind_path(path);
    if (kind == RE_CFG_KIND_NONE)
        kind = re_cfg_sniff((const char *)f.whole.p, f.whole.n);
    ok = re_keys_load_text(k, (const char *)f.whole.p, f.whole.n, kind);
    re_file_close(&f);
    re_arena_free(&a);
    return ok;
}

bool re_keys_autoload(re_keys_t *k) {
    char path[RE_CFG_PATH_MAX];
    if (!k)
        return false;
    re_keys_defaults(k);
    if (!re_cfg_find("ANTINOMIA_KEYS", "keys.json", "keys.ini", path, sizeof(path)))
        return false;
    if (!re_keys_load_path(k, path)) {
        re_keys_defaults(k);
        return false;
    }
    return true;
}
