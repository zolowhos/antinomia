// re_str.h - length-aware string helpers over (pointer, length) pairs.
// Module: util (C11).
// Owns: compare, search, trim, split, join, case folding and printability checks.
// Depends: re_arena and re_vec. No I/O, no globals, never calls strlen on input.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "utils/mem/re_arena.h"
#include "utils/mem/re_vec.h"

typedef struct {
    const char *p;
    size_t n;
} re_str_t;

static inline re_str_t re_str(const char *p) {
    re_str_t s = {p, 0};
    if (p)
        while (p[s.n])
            s.n++;
    return s;
}

static inline re_str_t re_strn(const char *p, size_t n) {
    re_str_t s = {p, p ? n : 0};
    return s;
}

static inline char re_char_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static inline char re_char_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

static inline bool re_str_eq(re_str_t a, re_str_t b) {
    if (a.n != b.n)
        return false;
    if (a.n == 0)
        return true;
    return memcmp(a.p, b.p, a.n) == 0;
}

static inline bool re_str_eq_cstr(re_str_t a, const char *b) {
    return re_str_eq(a, re_str(b));
}

static inline bool re_str_eq_n(re_str_t a, const char *b, size_t bn) {
    return re_str_eq(a, re_strn(b, bn));
}

static inline bool re_str_icmp(re_str_t a, re_str_t b) {
    size_t n = a.n < b.n ? a.n : b.n;
    for (size_t i = 0; i < n; i++) {
        char ca = re_char_lower(a.p[i]);
        char cb = re_char_lower(b.p[i]);
        if (ca != cb)
            return ca < cb;
    }
    return a.n < b.n;
}

// Case insensitive ordering against a C string, so callers can compare to zero.
static inline int re_str_cmp_cstr(re_str_t a, const char *b) {
    return re_str_icmp(a, re_str(b));
}

static inline bool re_str_ieq_cstr(re_str_t a, const char *b) {
    return a.n == re_str(b).n && re_str_cmp_cstr(a, b) == 0;
}

static inline bool re_str_starts(re_str_t a, re_str_t pre) {
    return pre.n <= a.n && (pre.n == 0 || memcmp(a.p, pre.p, pre.n) == 0);
}

static inline bool re_str_ends(re_str_t a, re_str_t suf) {
    return suf.n <= a.n && (suf.n == 0 || memcmp(a.p + a.n - suf.n, suf.p, suf.n) == 0);
}

static inline bool re_str_starts_cstr(re_str_t a, const char *pre) {
    return re_str_starts(a, re_str(pre));
}

static inline bool re_str_ends_cstr(re_str_t a, const char *suf) {
    return re_str_ends(a, re_str(suf));
}

static inline bool re_str_istarts_cstr(re_str_t a, const char *pre) {
    re_str_t p = re_str(pre);
    if (p.n > a.n)
        return false;
    for (size_t i = 0; i < p.n; i++) {
        if (re_char_lower(a.p[i]) != re_char_lower(p.p[i]))
            return false;
    }
    return true;
}

// Substring search over a bounded slice, so a missing NUL cannot run off the end.
static inline long re_str_find(re_str_t hay, re_str_t needle, size_t from) {
    if (needle.n == 0)
        return (long)from;
    if (hay.n < needle.n || from > hay.n - needle.n)
        return -1;
    for (size_t i = from; i <= hay.n - needle.n; i++) {
        if (hay.p[i] == needle.p[0] && memcmp(hay.p + i, needle.p, needle.n) == 0)
            return (long)i;
    }
    return -1;
}

static inline long re_str_find_cstr(re_str_t hay, const char *needle, size_t from) {
    return re_str_find(hay, re_str(needle), from);
}

static inline long re_str_rfind(re_str_t hay, re_str_t needle) {
    if (needle.n == 0)
        return (long)hay.n;
    if (hay.n < needle.n)
        return -1;
    for (size_t i = hay.n - needle.n + 1; i > 0; i--)
        if (hay.p[i - 1] == needle.p[0] && memcmp(hay.p + i - 1, needle.p, needle.n) == 0)
            return (long)(i - 1);
    return -1;
}

static inline long re_str_rfind_cstr(re_str_t hay, const char *needle) {
    return re_str_rfind(hay, re_str(needle));
}

static inline long re_str_rfind_char(re_str_t hay, char c, size_t from) {
    for (size_t i = from; i > 0; i--) {
        if (hay.p[i - 1] == c)
            return (long)(i - 1);
    }
    return -1;
}

static inline re_str_t re_str_trim(re_str_t s) {
    size_t b = 0;
    size_t e = s.n;
    while (b < e && (s.p[b] == ' ' || s.p[b] == '\t' || s.p[b] == '\r' || s.p[b] == '\n'))
        b++;
    while (e > b &&
           (s.p[e - 1] == ' ' || s.p[e - 1] == '\t' || s.p[e - 1] == '\r' || s.p[e - 1] == '\n'))
        e--;
    return re_strn(s.p + b, e - b);
}

static inline char *re_str_dup(re_arena_t *a, re_str_t s) {
    return re_arena_strndup(a, s.p, s.n);
}

static inline void re_str_to_lower(char *p, size_t n) {
    for (size_t i = 0; i < n; i++)
        p[i] = re_char_lower(p[i]);
}

// True when every byte is printable ASCII or tab, newline or carriage return.
static inline bool re_str_is_printable(re_str_t s) {
    for (size_t i = 0; i < s.n; i++) {
        uint8_t c = (uint8_t)s.p[i];
        if (c == '\t' || c == '\n' || c == '\r')
            continue;
        if (c < 0x20 || c > 0x7e)
            return false;
    }
    return true;
}

// Split on a single separator byte. Returns a vec of re_str_t views into hay,
// so no bytes are copied. Caller owns the vec; the views borrow the input.
static inline re_vec_t re_str_split(re_arena_t *a, re_str_t hay, char sep) {
    re_vec_t out;
    re_vec_init(&out, sizeof(re_str_t));
    size_t start = 0;
    for (size_t i = 0; i <= hay.n; i++) {
        if (i == hay.n || hay.p[i] == sep) {
            re_str_t piece = re_strn(hay.p + start, i - start);
            if (!RE_VEC_PUSH(&out, a, piece)) {
                out.len = 0;
                return out;
            }
            start = i + 1;
        }
    }
    return out;
}

// Join views with a separator into one arena string. Used for path and tag joins.
static inline char *re_str_join(re_arena_t *a, const re_vec_t *v, const char *sep) {
    re_str_t s = re_str(sep);
    size_t total = 0;
    for (size_t i = 0; i < v->len; i++)
        total += RE_VEC_AT(v, re_str_t, i).n + (i ? s.n : 0);
    char *out = (char *)re_arena_alloc(a, total + 1);
    if (!out)
        return NULL;
    size_t o = 0;
    for (size_t i = 0; i < v->len; i++) {
        if (i) {
            memcpy(out + o, s.p, s.n);
            o += s.n;
        }
        re_str_t piece = RE_VEC_AT(v, re_str_t, i);
        memcpy(out + o, piece.p, piece.n);
        o += piece.n;
    }
    out[o] = '\0';
    return out;
}
#ifdef __cplusplus
}
#endif
