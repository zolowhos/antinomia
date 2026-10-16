// re_hex.h - hex encode and decode using a lookup nibble table.
// Module: util (C11).
// Owns: byte to hex string, hex string to byte, single nibble conversion.
// Depends: none. No I/O, no globals, header-only inline, ASCII only.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const char re_hex_digits_lower[17] = "0123456789abcdef";

// Two hex characters for one byte value, no separators, no case control.
static inline char re_hex_nibble(uint8_t v) {
    return re_hex_digits_lower[v & 0x0fu];
}

// Nibble value of one hex char, or -1 when not a hex digit.
static inline int re_hex_val(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// Write 2*n characters plus a NUL into dst. dst must hold 2*n+1 bytes.
static inline void re_hex_encode(char *dst, const uint8_t *src, size_t n) {
    for (size_t i = 0; i < n; i++) {
        dst[i * 2] = re_hex_nibble((uint8_t)(src[i] >> 4));
        dst[i * 2 + 1] = re_hex_nibble(src[i]);
    }
    dst[n * 2] = '\0';
}

// Write 2*n characters plus a NUL, upper case, for xrefs and address tables.
static inline void re_hex_encode_upper(char *dst, const uint8_t *src, size_t n) {
    for (size_t i = 0; i < n; i++) {
        uint8_t hi = (uint8_t)(src[i] >> 4);
        uint8_t lo = (uint8_t)(src[i] & 0x0fu);
        dst[i * 2] = (char)(hi < 10 ? '0' + hi : 'A' + hi - 10);
        dst[i * 2 + 1] = (char)(lo < 10 ? '0' + lo : 'A' + lo - 10);
    }
    dst[n * 2] = '\0';
}

// Decode exactly n hex characters into n bytes. Returns false on any bad char or
// when dst_cap is too small. Ignores spaces, colons and 0x prefixes only if
// skip_sep is set.
static inline bool re_hex_decode(uint8_t *dst, size_t dst_cap, const char *src, size_t n,
                                 size_t *out_n, bool skip_sep) {
    size_t o = 0;
    int hi = -1;
    for (size_t i = 0; i < n; i++) {
        char c = src[i];
        if (skip_sep && (c == ' ' || c == ':' || c == '-' || c == '\t'))
            continue;
        int v = re_hex_val(c);
        if (v < 0)
            return false;
        if (hi < 0) {
            hi = v;
        } else {
            if (o >= dst_cap)
                return false;
            dst[o++] = (uint8_t)((hi << 4) | v);
            hi = -1;
        }
    }
    if (out_n)
        *out_n = o;
    return hi < 0;
}

// Length of the hex digit run at the start, skipping a leading 0x or 0X, so
// "0x1234zz" reports 4. Used when parsing addresses off the command line.
static inline size_t re_hex_run_len(const char *s, size_t n) {
    size_t i = 0;
    if (n >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        i = 2;
    while (i < n && re_hex_val(s[i]) >= 0)
        i++;
    return i - ((n >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? 2 : 0);
}
#ifdef __cplusplus
}
#endif
