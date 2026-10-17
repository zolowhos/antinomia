// re_theme.c - theme defaults, ini/json load, and TUI style install.
// Module: util (C11).
// Owns: re_theme.h. Style values are SGR bodies (e.g. "1;36"), not full escapes.
// Depends: re_theme.h, re_jr, re_buf, re_str.
#include "utils/cfg/re_theme.h"

#include "utils/json/re_jr.h"
#include "utils/mem/re_arena.h"
#include "utils/mem/re_buf.h"
#include "utils/text/re_str.h"

#include <string.h>

static void set_sgr(re_theme_t *t, re_style_t st, const char *sgr) {
    char esc[RE_CFG_SGR_MAX];
    size_t n = 0;
    if (!t || (unsigned)st > (unsigned)RE_ST_CALL || !sgr)
        return;
    while (sgr[n])
        n++;
    if (!re_cfg_sgr_esc(esc, sizeof(esc), sgr, n))
        return;
    re_cfg_copy(t->on[st], RE_CFG_SGR_MAX, esc, n + 3u);
    re_cfg_copy(t->off[st], RE_CFG_SGR_MAX, "\x1b[0m", 4u);
}

void re_theme_defaults(re_theme_t *t) {
    if (!t)
        return;
    memset(t, 0, sizeof(*t));
    re_cfg_copy(t->name, RE_THEME_NAME_MAX, "default", 7u);
    set_sgr(t, RE_ST_TITLE, "1;7");
    set_sgr(t, RE_ST_HEAD, "1");
    set_sgr(t, RE_ST_LABEL, "2");
    set_sgr(t, RE_ST_ACCENT, "1;36");
    set_sgr(t, RE_ST_GOOD, "32");
    set_sgr(t, RE_ST_WARN, "33");
    set_sgr(t, RE_ST_BAD, "1;31");
    set_sgr(t, RE_ST_HOVER, "38;2;12;36;64;48;2;186;220;255");
    set_sgr(t, RE_ST_PRESS, "38;2;232;242;255;48;2;15;55;130");
    set_sgr(t, RE_ST_KW, "1;35");
    set_sgr(t, RE_ST_TYPE, "36");
    set_sgr(t, RE_ST_CMT, "90");
    set_sgr(t, RE_ST_STR, "33");
    set_sgr(t, RE_ST_NUM, "31");
    set_sgr(t, RE_ST_CALL, "1;32");
}

void re_theme_apply(const re_theme_t *t) {
    unsigned i;
    if (!t) {
        re_tui_styles_reset();
        return;
    }
    for (i = 0; i <= (unsigned)RE_ST_CALL; i++) {
        const char *on = t->on[i][0] ? t->on[i] : "";
        const char *off = t->off[i][0] ? t->off[i] : "\x1b[0m";
        re_tui_style_set((re_style_t)i, on, off);
    }
}

static void apply_pair(re_theme_t *t, re_str_t key, re_str_t val) {
    int id = re_cfg_style_id(key.p, key.n);
    char esc[RE_CFG_SGR_MAX];
    if (id < 0 || val.n == 0)
        return;
    if (val.p[0] == '\x1b') {
        re_cfg_copy(t->on[id], RE_CFG_SGR_MAX, val.p, val.n);
        re_cfg_copy(t->off[id], RE_CFG_SGR_MAX, "\x1b[0m", 4u);
        return;
    }
    if (!re_cfg_sgr_esc(esc, sizeof(esc), val.p, val.n))
        return;
    re_cfg_copy(t->on[id], RE_CFG_SGR_MAX, esc, val.n + 3u);
    re_cfg_copy(t->off[id], RE_CFG_SGR_MAX, "\x1b[0m", 4u);
}

static bool load_json(re_theme_t *t, const char *text, size_t n) {
    re_arena_t a;
    re_jr_t root;
    const re_jr_t *styles;
    const re_jr_t *name;
    size_t i;
    re_arena_init(&a, 64u * 1024u);
    if (!re_jr_parse(&a, text, n, &root) || root.kind != RE_JR_OBJ) {
        re_arena_free(&a);
        return false;
    }
    name = re_jr_get(&root, "name");
    if (name && name->kind == RE_JR_STR)
        re_cfg_copy(t->name, RE_THEME_NAME_MAX, name->str.p, name->str.n);
    styles = re_jr_get(&root, "styles");
    if (!styles || styles->kind != RE_JR_OBJ) {
        re_arena_free(&a);
        return true; /* name-only theme is still valid */
    }
    for (i = 0; i < styles->count; i++) {
        re_str_t key = styles->keys[i];
        const re_jr_t *v = &styles->items[i];
        if (v->kind != RE_JR_STR)
            continue;
        apply_pair(t, key, v->str);
    }
    re_arena_free(&a);
    return true;
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

static void ini_line(re_theme_t *t, re_str_t line, int *section) {
    re_str_t key, val;
    long eq;
    if (line.n == 0 || line.p[0] == '#' || line.p[0] == ';')
        return;
    if (line.p[0] == '[') {
        *section = re_str_istarts_cstr(line, "[styles]")  ? 1
                   : re_str_istarts_cstr(line, "[theme]") ? 2
                                                          : 0;
        return;
    }
    eq = re_str_find_cstr(line, "=", 0);
    if (eq < 0)
        return;
    key = trim_span(line.p, (size_t)eq);
    val = trim_span(line.p + (size_t)eq + 1u, line.n - (size_t)eq - 1u);
    if (*section == 2 && re_str_ieq_cstr(key, "name")) {
        re_cfg_copy(t->name, RE_THEME_NAME_MAX, val.p, val.n);
        return;
    }
    if (*section == 1)
        apply_pair(t, key, val);
}

static bool load_ini(re_theme_t *t, const char *text, size_t n) {
    size_t i = 0;
    int section = 0;
    while (i < n) {
        size_t start = i;
        while (i < n && text[i] != '\n')
            i++;
        ini_line(t, trim_span(text + start, i - start), &section);
        if (i < n)
            i++;
    }
    return true;
}

bool re_theme_load_text(re_theme_t *t, const char *text, size_t n, re_cfg_kind_t kind) {
    if (!t || !text)
        return false;
    re_theme_defaults(t);
    if (kind == RE_CFG_KIND_NONE)
        kind = re_cfg_sniff(text, n);
    if (kind == RE_CFG_KIND_JSON)
        return load_json(t, text, n);
    if (kind == RE_CFG_KIND_INI)
        return load_ini(t, text, n);
    return false;
}

bool re_theme_load_path(re_theme_t *t, const char *path) {
    re_arena_t a;
    re_file_t f;
    re_cfg_kind_t kind;
    bool ok;
    if (!t || !path)
        return false;
    re_arena_init(&a, 4u * 1024u);
    if (re_file_open(path, &a, &f) != RE_OK) {
        re_arena_free(&a);
        return false;
    }
    kind = re_cfg_kind_path(path);
    if (kind == RE_CFG_KIND_NONE)
        kind = re_cfg_sniff((const char *)f.whole.p, f.whole.n);
    ok = re_theme_load_text(t, (const char *)f.whole.p, f.whole.n, kind);
    re_file_close(&f);
    re_arena_free(&a);
    return ok;
}

bool re_theme_autoload(re_theme_t *t) {
    char path[RE_CFG_PATH_MAX];
    if (!t)
        return false;
    re_theme_defaults(t);
    if (!re_cfg_find("ANTINOMIA_THEME", "theme.json", "theme.ini", path, sizeof(path))) {
        re_theme_apply(t);
        return false;
    }
    if (!re_theme_load_path(t, path)) {
        re_theme_defaults(t);
        re_theme_apply(t);
        return false;
    }
    re_theme_apply(t);
    return true;
}
