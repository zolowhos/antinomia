// re_fmt.h - number and padding formatting that re_strbuf does not already cover.
// Module: util (C11).
// Owns: signed integers, floats, arbitrary base, column padding, byte sizes.
// Depends: re_strbuf. No I/O, no globals, writes only into the caller's buffer.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "utils/text/re_strbuf.h"

static inline bool re_fmt_put_i64(re_strbuf_t *b, int64_t v) {
    if (v < 0) {
        if (!re_strbuf_putc(b, '-'))
            return false;
        return re_strbuf_put_u64(b, (uint64_t)0 - (uint64_t)v);
    }
    return re_strbuf_put_u64(b, (uint64_t)v);
}

static inline bool re_fmt_put_base(re_strbuf_t *b, uint64_t v, unsigned base) {
    if (base < 2 || base > 16)
        return false;
    if (v == 0)
        return re_strbuf_putc(b, '0');
    char tmp[24];
    int n = 0;
    while (v) {
        tmp[n++] = re_hex_nibble((uint8_t)(v % base));
        v /= base;
    }
    if (!re_strbuf_reserve(b, (size_t)n))
        return false;
    for (int i = n - 1; i >= 0; i--)
        b->p[b->len++] = tmp[i];
    b->p[b->len] = '\0';
    return true;
}

static inline bool re_fmt_put_f64(re_strbuf_t *b, double v) {
    return re_strbuf_appendf(b, "%g", v);
}

// Pad to a column. Right aligns, which is what the text renderer wants for the
// address and size columns.
static inline bool re_fmt_put_pad(re_strbuf_t *b, re_str_t s, size_t width, bool right) {
    if (s.n >= width)
        return re_strbuf_put_re_str(b, s);
    size_t fill = width - s.n;
    if (!re_strbuf_reserve(b, fill))
        return false;
    if (right) {
        memset(b->p + b->len, ' ', fill);
        b->len += fill;
        b->p[b->len] = '\0';
        return re_strbuf_put_re_str(b, s);
    }
    if (!re_strbuf_put_re_str(b, s))
        return false;
    if (!re_strbuf_reserve(b, fill))
        return false;
    memset(b->p + b->len, ' ', fill);
    b->len += fill;
    b->p[b->len] = '\0';
    return true;
}

// Human readable byte count for the text view: 1536 becomes 1.5K.
static inline bool re_fmt_put_size(re_strbuf_t *b, uint64_t bytes) {
    static const char *units[] = {"B", "K", "M", "G", "T"};
    if (bytes < 1024)
        return re_strbuf_appendf(b, "%lluB", (unsigned long long)bytes);
    double v = (double)bytes;
    unsigned u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        u++;
    }
    return re_strbuf_appendf(b, "%.1f%s", v, units[u]);
}
#ifdef __cplusplus
}
#endif
