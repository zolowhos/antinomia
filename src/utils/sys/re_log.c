// re_log.c - the only stderr writer in the project, kept tiny and dependency free.
// Module: util (C11).
// Owns: level state, mute flag, environment parsing, formatted stderr output.
// Depends: re_log.h only. Writes stderr exclusively, never stdout.
#include "utils/sys/re_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static re_log_level_t g_level = RE_LOG_WARN;
static bool g_mute = false;
static bool g_env_done = false;

const char *re_log_level_name(re_log_level_t lvl) {
    switch (lvl) {
        case RE_LOG_OFF:
            return "off";
        case RE_LOG_ERROR:
            return "error";
        case RE_LOG_WARN:
            return "warn";
        case RE_LOG_INFO:
            return "info";
        default:
            return "debug";
    }
}

re_log_level_t re_log_level(void) {
    return g_level;
}

void re_log_set_level(re_log_level_t lvl) {
    g_level = lvl;
}

void re_log_set_mute(bool mute) {
    g_mute = mute;
}

bool re_log_muted(void) {
    return g_mute;
}

void re_log_init_from_env(void) {
    if (g_env_done)
        return;
    g_env_done = true;
    const char *e = getenv("RE_LOG");
    if (!e || !e[0])
        return;
    if (strcmp(e, "off") == 0)
        g_level = RE_LOG_OFF;
    else if (strcmp(e, "error") == 0)
        g_level = RE_LOG_ERROR;
    else if (strcmp(e, "warn") == 0)
        g_level = RE_LOG_WARN;
    else if (strcmp(e, "info") == 0)
        g_level = RE_LOG_INFO;
    else if (strcmp(e, "debug") == 0)
        g_level = RE_LOG_DEBUG;
}

void re_log_emit(re_log_level_t lvl, const char *file, int line, const char *fmt, ...) {
    if (g_mute)
        return;
    const char *base = file ? file : "?";
    const char *slash = strrchr(base, '/');
    const char *bslash = strrchr(base, '\\');
    if (slash && (!bslash || slash > bslash))
        base = slash + 1;
    else if (bslash)
        base = bslash + 1;
    fprintf(stderr, "re %s %s:%d: ", re_log_level_name(lvl), base, line);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}
