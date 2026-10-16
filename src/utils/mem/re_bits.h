// re_bits.h - bit counting, scanning, rotation, byte swap and alignment helpers.
// Module: util (C11).
// Owns: popcount, ctz, clz, rotate, byte swap, align_up over fixed width integers.
// Depends: none. No I/O, no globals, header-only inline, undefined for zero input.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_MSC_VER)
#    include <intrin.h>
#endif

// Population count. Defined for all inputs including zero.
static inline unsigned re_bits_popcount(uint64_t v) {
#if defined(_MSC_VER)
    return (unsigned)__popcnt64(v);
#elif defined(__GNUC__)
    return (unsigned)__builtin_popcountll(v);
#else
    v = v - ((v >> 1) & 0x5555555555555555ULL);
    v = (v & 0x3333333333333333ULL) + ((v >> 2) & 0x3333333333333333ULL);
    v = (v + (v >> 4)) & 0x0f0f0f0f0f0f0f0fULL;
    return (unsigned)((v * 0x0101010101010101ULL) >> 56);
#endif
}

// Count trailing zeros, index of lowest set bit. Callers must pass non-zero.
static inline unsigned re_bits_ctz64(uint64_t v) {
#if defined(_MSC_VER)
    unsigned long i;
    _BitScanForward64(&i, v);
    return (unsigned)i;
#elif defined(__GNUC__)
    return (unsigned)__builtin_ctzll(v);
#else
    unsigned n = 0;
    if (!v)
        return 64;
    while (!(v & 1u)) {
        v >>= 1;
        n++;
    }
    return n;
#endif
}

// Count leading zeros. Callers must pass non-zero.
static inline unsigned re_bits_clz64(uint64_t v) {
#if defined(_MSC_VER)
    unsigned long i;
    _BitScanReverse64(&i, v);
    return 63u - (unsigned)i;
#elif defined(__GNUC__)
    return (unsigned)__builtin_clzll(v);
#else
    unsigned n = 0;
    if (!v)
        return 64;
    while (!(v >> 63)) {
        v <<= 1;
        n++;
    }
    return n;
#endif
}

static inline uint64_t re_bits_rotl64(uint64_t v, unsigned n) {
    n &= 63u;
    return n ? ((v << n) | (v >> (64 - n))) : v;
}

static inline uint64_t re_bits_rotr64(uint64_t v, unsigned n) {
    n &= 63u;
    return n ? ((v >> n) | (v << (64 - n))) : v;
}

// Byte swap, for the big endian ELF and Mach-O readers.
static inline uint16_t re_bswap16(uint16_t v) {
    return (uint16_t)((v >> 8) | (v << 8));
}

static inline uint32_t re_bswap32(uint32_t v) {
    return ((v & 0x000000ffu) << 24) | ((v & 0x0000ff00u) << 8) | ((v & 0x00ff0000u) >> 8) |
           ((v & 0xff000000u) >> 24);
}

static inline uint64_t re_bswap64(uint64_t v) {
    return ((uint64_t)re_bswap32((uint32_t)v) << 32) | (uint64_t)re_bswap32((uint32_t)(v >> 32));
}

static inline bool re_bits_is_pow2(size_t v) {
    return v != 0 && (v & (v - 1)) == 0;
}

// Round v up to the next multiple of a. a must be a power of two.
static inline size_t re_align_up(size_t v, size_t a) {
    return (v + (a - 1)) & ~(a - 1);
}

static inline size_t re_align_down(size_t v, size_t a) {
    return v & ~(a - 1);
}

static inline uint64_t re_min_u64(uint64_t a, uint64_t b) {
    return a < b ? a : b;
}

static inline uint64_t re_max_u64(uint64_t a, uint64_t b) {
    return a > b ? a : b;
}

static inline size_t re_min_sz(size_t a, size_t b) {
    return a < b ? a : b;
}

static inline size_t re_max_sz(size_t a, size_t b) {
    return a > b ? a : b;
}
#ifdef __cplusplus
}
#endif
