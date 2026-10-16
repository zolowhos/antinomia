// re_strbuf.h - growable NUL terminated byte buffer for building strings in place.
// Module: util (C11).
// Owns: append, putc, puts, printf style formatting, hex and byte appends.
// Depends: re_arena, re_str and libc vsnprintf. No I/O, no globals, never shrinks.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "utils/mem/re_arena.h"
#include "utils/text/re_hex.h"
#include "utils/text/re_str.h"

typedef struct {
    char *p;
    size_t len;
    size_t cap;
    re_arena_t *arena;
} re_strbuf_t;

static inline void re_strbuf_init(re_strbuf_t *b, re_arena_t *a) {
    b->p = NULL;
    b->len = 0;
    b->cap = 0;
    b->arena = a;
}

static inline bool re_strbuf_reserve(re_strbuf_t *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap)
        return true;
    size_t cap = b->cap ? b->cap : 128;
    while (cap < b->len + extra + 1)
        cap *= 2;
    char *np = (char *)re_arena_alloc(b->arena, cap);
    if (!np)
        return false;
    if (b->p && b->len)
        memcpy(np, b->p, b->len);
    b->p = np;
    b->cap = cap;
    return true;
}

static inline bool re_strbuf_append(re_strbuf_t *b, const void *data, size_t n) {
    if (!n)
        return true;
    if (!re_strbuf_reserve(b, n))
        return false;
    memcpy(b->p + b->len, data, n);
    b->len += n;
    b->p[b->len] = '\0';
    return true;
}

static inline bool re_strbuf_putc(re_strbuf_t *b, char c) {
    return re_strbuf_append(b, &c, 1);
}

static inline bool re_strbuf_puts(re_strbuf_t *b, const char *s) {
    return re_strbuf_append(b, s, strlen(s));
}

static inline bool re_strbuf_put_re_str(re_strbuf_t *b, re_str_t s) {
    return re_strbuf_append(b, s.p, s.n);
}

static inline bool re_strbuf_put_hex(re_strbuf_t *b, const uint8_t *d, size_t n) {
    if (!re_strbuf_reserve(b, n * 2))
        return false;
    re_hex_encode(b->p + b->len, d, n);
    b->len += n * 2;
    return true;
}

static inline bool re_strbuf_put_u64(re_strbuf_t *b, uint64_t v) {
    char tmp[24];
    int n = 0;
    if (v == 0)
        tmp[n++] = '0';
    while (v) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    if (!re_strbuf_reserve(b, (size_t)n))
        return false;
    for (int i = n - 1; i >= 0; i--)
        b->p[b->len++] = tmp[i];
    b->p[b->len] = '\0';
    return true;
}

static inline bool re_strbuf_put_hex64(re_strbuf_t *b, uint64_t v, int digits) {
    if (digits < 1 || digits > 16)
        digits = 16;
    if (!re_strbuf_reserve(b, (size_t)digits))
        return false;
    for (int i = 0; i < digits; i++) {
        unsigned shift = (unsigned)(digits - 1 - i) * 4u;
        b->p[b->len + (size_t)i] = re_hex_nibble((uint8_t)((v >> shift) & 0x0fu));
    }
    b->len += (size_t)digits;
    b->p[b->len] = '\0';
    return true;
}

// Formats into the buffer via a stack buffer first, so the common short case
// costs no arena allocation beyond the buffer's own growth.
static inline bool re_strbuf_appendf(re_strbuf_t *b, const char *fmt, ...) {
    va_list ap;
    va_list ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    char stack[256];
    int n = vsnprintf(stack, sizeof(stack), fmt, ap);
    va_end(ap);
    bool ok = false;
    if (n < 0) {
        va_end(ap2);
        return false;
    }
    if ((size_t)n < sizeof(stack)) {
        ok = re_strbuf_append(b, stack, (size_t)n);
    } else if (re_strbuf_reserve(b, (size_t)n)) {
        ok = vsnprintf(b->p + b->len, (size_t)n + 1, fmt, ap2) == n;
        if (ok)
            b->len += (size_t)n;
    }
    va_end(ap2);
    return ok;
}

// Terminate and hand ownership of the buffer to the caller. The buffer stays
// valid for the arena's lifetime, not just until the next append.
static inline char *re_strbuf_detach(re_strbuf_t *b) {
    if (!b->p)
        return NULL;
    b->p[b->len] = '\0';
    return b->p;
}

static inline void re_strbuf_clear(re_strbuf_t *b) {
    b->len = 0;
    if (b->p)
        b->p[0] = '\0';
}
#ifdef __cplusplus
}
#endif
