// re_syntax.c - token colours for the pseudocode pane.
// Module: util (C11).
// Owns: re_syntax_put, the walk that styles one line onto a screen row.
// Depends: re_screen, re_str. No allocation, no I/O, no mutable globals.
#include "utils/tui/re_syntax.h"

#include "utils/text/re_str.h"

static bool ident_start(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static bool ident_more(unsigned char c) {
    return ident_start(c) || (c >= '0' && c <= '9');
}

static bool listed(const char *p, size_t n, const char *const *words) {
    re_str_t w;
    w.p = p;
    w.n = n;
    for (size_t i = 0; words[i]; i++)
        if (re_str_eq_cstr(w, words[i]))
            return true;
    return false;
}

static bool is_label(const char *p, size_t n) {
    size_t i = 1;
    if (n < 2 || p[0] != 'L')
        return false;
    while (i < n && p[i] >= '0' && p[i] <= '9')
        i++;
    return i == n;
}

static bool is_call(const char *rest) {
    while (*rest == ' ')
        rest++;
    return *rest == '(';
}

static uint8_t word_style(const char *p, size_t n, const char *rest) {
    static const char *kKw[] = {"if",     "goto", "return", "else",     "while",  "for", "do",
                                "switch", "case", "break",  "continue", "sizeof", NULL};
    static const char *kTy[] = {"void",     "int",    "char",    "long",    "short",    "unsigned",
                                "signed",   "bool",   "const",   "uint8_t", "uint16_t", "uint32_t",
                                "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t",  NULL};
    if (listed(p, n, kKw))
        return (uint8_t)RE_ST_KW;
    if (listed(p, n, kTy))
        return (uint8_t)RE_ST_TYPE;
    if (is_label(p, n))
        return (uint8_t)RE_ST_ACCENT;
    if (is_call(rest))
        return (uint8_t)RE_ST_CALL;
    return (uint8_t)RE_ST_NONE;
}

static size_t take_string(const char *p) {
    size_t n = 1;
    while (p[n] && p[n] != '"') {
        if (p[n] == '\\' && p[n + 1])
            n += 2;
        else
            n++;
    }
    if (p[n] == '"')
        n++;
    return n;
}

static size_t take_number(const char *p) {
    size_t n = 0;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        n = 2;
        while ((p[n] >= '0' && p[n] <= '9') || (p[n] >= 'a' && p[n] <= 'f') ||
               (p[n] >= 'A' && p[n] <= 'F'))
            n++;
        return n;
    }
    while (p[n] >= '0' && p[n] <= '9')
        n++;
    return n ? n : 1;
}

static uint16_t put_bytes(re_screen_t *s, uint16_t y, uint16_t x, uint16_t limit, const char *p,
                          size_t n, uint8_t st) {
    size_t i = 0;
    while (i < n && x < limit && p[i]) {
        char g[2];
        g[0] = p[i];
        g[1] = '\0';
        re_screen_put(s, y, x, g, st, RE_SCREEN_ZONE_NONE);
        i++;
        x++;
    }
    return x;
}

uint16_t re_syntax_put(re_screen_t *s, uint16_t y, uint16_t x, uint16_t limit, const char *line) {
    const char *p = line ? line : "";
    while (*p && x < limit) {
        size_t n = 1;
        uint8_t st = (uint8_t)RE_ST_NONE;
        unsigned char c = (unsigned char)p[0];
        if (p[0] == '/' && p[1] == '/') {
            n = 0;
            while (p[n])
                n++;
            st = (uint8_t)RE_ST_CMT;
        } else if (p[0] == '"') {
            n = take_string(p);
            st = (uint8_t)RE_ST_STR;
        } else if (c >= '0' && c <= '9') {
            n = take_number(p);
            st = (uint8_t)RE_ST_NUM;
        } else if (ident_start(c)) {
            n = 1;
            while (ident_more((unsigned char)p[n]))
                n++;
            st = word_style(p, n, p + n);
        }
        x = put_bytes(s, y, x, limit, p, n, st);
        p += n;
    }
    return x;
}
