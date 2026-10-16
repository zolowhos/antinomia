// re_dbg_x86.c - minimal x86_64 CALL length decode for step-over.
// Module: feature (C11).
// Owns: near/indirect call recognition without Capstone.
// Depends: re_dbg_x86.h. ASCII-only; no malloc.
#include "features/debug/re_dbg_x86.h"

static size_t modrm_len(uint8_t modrm, const uint8_t *rest, size_t rest_n, bool rex_w_unused) {
    uint8_t mod = (uint8_t)(modrm >> 6);
    uint8_t rm = (uint8_t)(modrm & 7u);
    size_t n = 1;
    (void)rex_w_unused;
    if (mod == 3)
        return n;
    if (rm == 4) {
        if (rest_n < n + 1)
            return 0;
        n += 1; /* SIB */
    }
    if (mod == 1) {
        if (rest_n < n + 1)
            return 0;
        return n + 1;
    }
    if (mod == 2) {
        if (rest_n < n + 4)
            return 0;
        return n + 4;
    }
    /* mod == 0 */
    if (rm == 5) {
        if (rest_n < n + 4)
            return 0;
        return n + 4;
    }
    if (rm == 4 && rest_n >= 2) {
        uint8_t sib = rest[1];
        uint8_t base = (uint8_t)(sib & 7u);
        if (base == 5) {
            if (rest_n < n + 4)
                return 0;
            return n + 4;
        }
    }
    return n;
}

bool re_dbg_x86_is_call(const uint8_t *buf, size_t cap, size_t *len_out) {
    size_t i = 0;
    uint8_t rex = 0;
    if (!buf || !len_out || cap == 0)
        return false;
    *len_out = 0;
    while (i < cap && (buf[i] & 0xf0u) == 0x40u) {
        rex = buf[i];
        i++;
        if (i > 1)
            return false; /* only one REX */
    }
    if (i >= cap)
        return false;
    if (buf[i] == 0xe8u) {
        if (cap < i + 5)
            return false;
        *len_out = i + 5;
        return true;
    }
    if (buf[i] == 0xffu) {
        uint8_t modrm;
        size_t mlen;
        if (cap < i + 2)
            return false;
        modrm = buf[i + 1];
        if (((modrm >> 3) & 7u) != 2u)
            return false; /* /2 = call r/m */
        mlen = modrm_len(modrm, buf + i + 1, cap - (i + 1), (rex & 8u) != 0);
        if (mlen == 0)
            return false;
        *len_out = i + 1 + mlen;
        return true;
    }
    (void)rex;
    return false;
}
