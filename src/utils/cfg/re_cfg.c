// re_cfg.c - config path search and shared parsing helpers.
// Module: util (C11).
// Owns: re_cfg.h path discovery and SGR/name helpers.
// Depends: re_cfg.h, re_str, re_path. Uses getenv and access only.
#include "utils/cfg/re_cfg.h"

#include "utils/sys/re_path.h"
#include "utils/text/re_str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#    include <io.h>
#    define RE_CFG_ACCESS(p) (_access((p), 0) == 0)
#else
#    include <unistd.h>
#    define RE_CFG_ACCESS(p) (access((p), R_OK) == 0)
#endif

void re_cfg_copy(char *dst, size_t cap, const char *src, size_t n) {
    size_t i = 0;
    if (!dst || cap == 0)
        return;
    if (!src)
        n = 0;
    while (i + 1u < cap && i < n) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static bool try_path(char *dst, size_t cap, const char *path) {
    size_t n;
    if (!path || !path[0] || !RE_CFG_ACCESS(path))
        return false;
    n = 0;
    while (path[n])
        n++;
    if (n + 1u > cap)
        return false;
    re_cfg_copy(dst, cap, path, n);
    return true;
}

static bool join_parts(char *dst, size_t cap, const char *const *parts, size_t nparts) {
    size_t i = 0;
    size_t p;
    if (!dst || cap == 0)
        return false;
    dst[0] = 0;
    for (p = 0; p < nparts; p++) {
        const char *s = parts[p];
        size_t n = 0;
        if (!s || !s[0])
            continue;
        while (s[n])
            n++;
        if (i && i + 1u < cap && dst[i - 1] != '/' && dst[i - 1] != '\\')
            dst[i++] = '/';
        if (i + n + 1u > cap)
            return false;
        memcpy(dst + i, s, n);
        i += n;
        dst[i] = 0;
    }
    return i > 0;
}

static bool try_under(char *dst, size_t cap, const char *root, const char *file) {
    char path[RE_CFG_PATH_MAX];
    const char *parts[3];
    if (!root || !file)
        return false;
    parts[0] = root;
    parts[1] = "antinomia";
    parts[2] = file;
    if (!join_parts(path, sizeof(path), parts, 3))
        return false;
    return try_path(dst, cap, path);
}

static bool try_home_config(char *dst, size_t cap, const char *file) {
    const char *home = getenv("HOME");
    char root[RE_CFG_PATH_MAX];
    const char *parts[2];
    if (!home || !file)
        return false;
    parts[0] = home;
    parts[1] = ".config";
    if (!join_parts(root, sizeof(root), parts, 2))
        return false;
    return try_under(dst, cap, root, file);
}

bool re_cfg_find(const char *env_name, const char *base_json, const char *base_ini, char *dst,
                 size_t cap) {
    const char *env;
    const char *home;
    if (!dst || cap == 0)
        return false;
    dst[0] = 0;
    env = env_name ? getenv(env_name) : NULL;
    if (env && env[0] && try_path(dst, cap, env))
        return true;
    if (base_json && try_path(dst, cap, base_json))
        return true;
    if (base_ini && try_path(dst, cap, base_ini))
        return true;
#if defined(_WIN32)
    home = getenv("APPDATA");
    if (home && base_json && try_under(dst, cap, home, base_json))
        return true;
    if (home && base_ini && try_under(dst, cap, home, base_ini))
        return true;
#else
    home = getenv("XDG_CONFIG_HOME");
    if (home && base_json && try_under(dst, cap, home, base_json))
        return true;
    if (home && base_ini && try_under(dst, cap, home, base_ini))
        return true;
    if (base_json && try_home_config(dst, cap, base_json))
        return true;
    if (base_ini && try_home_config(dst, cap, base_ini))
        return true;
#endif
    return false;
}

re_cfg_kind_t re_cfg_sniff(const char *text, size_t n) {
    size_t i = 0;
    while (i < n && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n'))
        i++;
    if (i < n && text[i] == '{')
        return RE_CFG_KIND_JSON;
    if (i < n && text[i] == '[')
        return RE_CFG_KIND_INI;
    return RE_CFG_KIND_NONE;
}

re_cfg_kind_t re_cfg_kind_path(const char *path) {
    size_t off;
    if (!path)
        return RE_CFG_KIND_NONE;
    off = re_path_ext_off(path);
    if (off == RE_PATH_NPOS)
        return RE_CFG_KIND_NONE;
    if (re_str_ieq_cstr(re_str(path + off), ".json"))
        return RE_CFG_KIND_JSON;
    if (re_str_ieq_cstr(re_str(path + off), ".ini"))
        return RE_CFG_KIND_INI;
    return RE_CFG_KIND_NONE;
}

int re_cfg_style_id(const char *name, size_t n) {
    re_str_t s = re_strn(name, n);
    if (re_str_ieq_cstr(s, "none"))
        return (int)RE_ST_NONE;
    if (re_str_ieq_cstr(s, "title"))
        return (int)RE_ST_TITLE;
    if (re_str_ieq_cstr(s, "head"))
        return (int)RE_ST_HEAD;
    if (re_str_ieq_cstr(s, "label"))
        return (int)RE_ST_LABEL;
    if (re_str_ieq_cstr(s, "accent"))
        return (int)RE_ST_ACCENT;
    if (re_str_ieq_cstr(s, "good"))
        return (int)RE_ST_GOOD;
    if (re_str_ieq_cstr(s, "warn"))
        return (int)RE_ST_WARN;
    if (re_str_ieq_cstr(s, "bad"))
        return (int)RE_ST_BAD;
    if (re_str_ieq_cstr(s, "hover"))
        return (int)RE_ST_HOVER;
    if (re_str_ieq_cstr(s, "press"))
        return (int)RE_ST_PRESS;
    if (re_str_ieq_cstr(s, "kw") || re_str_ieq_cstr(s, "keyword"))
        return (int)RE_ST_KW;
    if (re_str_ieq_cstr(s, "type"))
        return (int)RE_ST_TYPE;
    if (re_str_ieq_cstr(s, "cmt") || re_str_ieq_cstr(s, "comment"))
        return (int)RE_ST_CMT;
    if (re_str_ieq_cstr(s, "str") || re_str_ieq_cstr(s, "string"))
        return (int)RE_ST_STR;
    if (re_str_ieq_cstr(s, "num") || re_str_ieq_cstr(s, "number"))
        return (int)RE_ST_NUM;
    if (re_str_ieq_cstr(s, "call"))
        return (int)RE_ST_CALL;
    return -1;
}

bool re_cfg_sgr_esc(char *dst, size_t cap, const char *sgr, size_t n) {
    size_t i;
    if (!dst || cap < 4u || !sgr || n == 0 || n + 3u >= cap)
        return false;
    dst[0] = '\x1b';
    dst[1] = '[';
    for (i = 0; i < n; i++)
        dst[2 + i] = sgr[i];
    dst[2 + n] = 'm';
    dst[3 + n] = 0;
    return true;
}
