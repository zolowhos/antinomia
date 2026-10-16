// re_hash.c - MD5 and SHA-256, compact and table driven, for identity hashing.
// Module: util (C11).
// Owns: the 64 round constant tables and the block transforms for both digests.
// Depends: re_hash.h only. No I/O, no allocation, no globals beyond const tables.
#include "utils/algo/re_hash.h"

#include <stdbool.h>
#include <string.h>

static const uint32_t MD5_K[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu, 0x4787c62au, 0xa8304613u,
    0xfd469501u, 0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu, 0x6b901122u, 0xfd987193u,
    0xa679438eu, 0x49b40821u, 0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau, 0xd62f105du,
    0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u, 0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
    0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au, 0xfffa3942u, 0x8771f681u, 0x6d9d6122u,
    0xfde5380cu, 0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u, 0x289b7ec6u, 0xeaa127fau,
    0xd4ef3085u, 0x04881d05u, 0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u, 0xf4292244u,
    0x432aff97u, 0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u, 0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu,
    0xeb86d391u};

static const uint8_t MD5_S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                  5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                  4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                  6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

static const uint32_t SHA256_K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

static uint32_t rotl32(uint32_t v, unsigned n) {
    return (uint32_t)((v << n) | (v >> (32 - n)));
}

static uint32_t rotr32(uint32_t v, unsigned n) {
    return (uint32_t)((v >> n) | (v << (32 - n)));
}

// Load 16 little endian words, which is what MD5 specifies.
static void load_le32(const uint8_t *p, uint32_t *w) {
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] | ((uint32_t)p[i * 4 + 1] << 8) | ((uint32_t)p[i * 4 + 2] << 16) |
               ((uint32_t)p[i * 4 + 3] << 24);
}

// Load 16 big endian words, which is what SHA-256 specifies. Sharing the little
// endian loader with MD5 is a silent, total corruption of every SHA-256 digest.
static void load_be32(const uint8_t *p, uint32_t *w) {
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
               ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
}

static void store_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

// SHA-256 emits its digest big endian, the mirror of MD5. Getting this wrong
// byte reverses every word and still produces a stable, wrong, plausible hex.
static void store_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void md5_block(void *vctx, const uint8_t *p) {
    re_md5_t *c = (re_md5_t *)vctx;
    uint32_t m[16];
    load_le32(p, m);
    uint32_t a = c->s[0], b = c->s[1], cc = c->s[2], d = c->s[3];
    for (int i = 0; i < 64; i++) {
        uint32_t f;
        unsigned g;
        if (i < 16) {
            f = (b & cc) | (~b & d);
            g = (unsigned)i;
        } else if (i < 32) {
            f = (d & b) | (~d & cc);
            g = (5u * (unsigned)i + 1u) & 15u;
        } else if (i < 48) {
            f = b ^ cc ^ d;
            g = (3u * (unsigned)i + 5u) & 15u;
        } else {
            f = cc ^ (b | ~d);
            g = (7u * (unsigned)i) & 15u;
        }
        uint32_t tmp = d;
        d = cc;
        cc = b;
        b = b + rotl32(a + f + MD5_K[i] + m[g], MD5_S[i]);
        a = tmp;
    }
    c->s[0] += a;
    c->s[1] += b;
    c->s[2] += cc;
    c->s[3] += d;
}

static void sha256_block(void *vctx, const uint8_t *p) {
    re_sha256_t *c = (re_sha256_t *)vctx;
    uint32_t w[64];
    load_be32(p, w);
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = c->s[0], b = c->s[1], cc = c->s[2], d = c->s[3];
    uint32_t e = c->s[4], f = c->s[5], gg = c->s[6], h = c->s[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ (~e & gg);
        uint32_t t1 = h + S1 + ch + SHA256_K[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t mj = (a & b) ^ (a & cc) ^ (b & cc);
        uint32_t t2 = S0 + mj;
        h = gg;
        gg = f;
        f = e;
        e = d + t1;
        d = cc;
        cc = b;
        b = a;
        a = t1 + t2;
    }
    c->s[0] += a;
    c->s[1] += b;
    c->s[2] += cc;
    c->s[3] += d;
    c->s[4] += e;
    c->s[5] += f;
    c->s[6] += gg;
    c->s[7] += h;
}

// Shared buffering: keep 64 byte blocks, hash the rest, remember the bit count.
static void absorb(const uint8_t *data, size_t n, uint64_t *bits, uint8_t *buf, size_t *fill,
                   void (*fn)(void *, const uint8_t *), void *c) {
    *bits += (uint64_t)n * 8u;
    while (n) {
        size_t take = 64 - *fill;
        if (take > n)
            take = n;
        memcpy(buf + *fill, data, take);
        *fill += take;
        data += take;
        n -= take;
        if (*fill == 64) {
            fn(c, buf);
            *fill = 0;
        }
    }
}

// MD5 ends the block with a little endian bit count, SHA-256 with a big endian
// one, so the order is a parameter rather than a shared assumption.
static void pad_and_flush(const uint8_t *tail, size_t tail_n, uint64_t bits, bool be_len,
                          void (*fn)(void *, const uint8_t *), void *c) {
    uint8_t last[128];
    size_t total = tail_n;
    memcpy(last, tail, tail_n);
    last[total++] = 0x80;
    size_t want = (total <= 56) ? 64 : 128;
    while (total < want - 8)
        last[total++] = 0;
    for (int i = 0; i < 8; i++) {
        unsigned sh = be_len ? (unsigned)(56 - i * 8) : (unsigned)(i * 8);
        last[want - 8 + (size_t)i] = (uint8_t)(bits >> sh);
    }
    for (size_t off = 0; off < want; off += 64)
        fn(c, last + off);
}

void re_md5_init(re_md5_t *c) {
    c->s[0] = 0x67452301u;
    c->s[1] = 0xefcdab89u;
    c->s[2] = 0x98badcfeu;
    c->s[3] = 0x10325476u;
    c->bits = 0;
    c->n = 0;
}

void re_md5_update(re_md5_t *c, const void *data, size_t n) {
    absorb((const uint8_t *)data, n, &c->bits, c->buf, &c->n, md5_block, c);
}

void re_md5_final(re_md5_t *c, uint8_t out[RE_MD5_LEN]) {
    uint8_t tail[64];
    size_t t = c->n;
    memcpy(tail, c->buf, t);
    pad_and_flush(tail, t, c->bits, false, md5_block, c);
    for (int i = 0; i < 4; i++)
        store_le32(out + i * 4, c->s[i]);
}

void re_md5(const void *data, size_t n, uint8_t out[RE_MD5_LEN]) {
    re_md5_t c;
    re_md5_init(&c);
    re_md5_update(&c, data, n);
    re_md5_final(&c, out);
}

void re_sha256_init(re_sha256_t *c) {
    c->s[0] = 0x6a09e667u;
    c->s[1] = 0xbb67ae85u;
    c->s[2] = 0x3c6ef372u;
    c->s[3] = 0xa54ff53au;
    c->s[4] = 0x510e527fu;
    c->s[5] = 0x9b05688cu;
    c->s[6] = 0x1f83d9abu;
    c->s[7] = 0x5be0cd19u;
    c->bits = 0;
    c->n = 0;
}

void re_sha256_update(re_sha256_t *c, const void *data, size_t n) {
    absorb((const uint8_t *)data, n, &c->bits, c->buf, &c->n, sha256_block, c);
}

void re_sha256_final(re_sha256_t *c, uint8_t out[RE_SHA256_LEN]) {
    uint8_t tail[64];
    size_t t = c->n;
    memcpy(tail, c->buf, t);
    pad_and_flush(tail, t, c->bits, true, sha256_block, c);
    for (int i = 0; i < 8; i++)
        store_be32(out + i * 4, c->s[i]);
}

void re_sha256(const void *data, size_t n, uint8_t out[RE_SHA256_LEN]) {
    re_sha256_t c;
    re_sha256_init(&c);
    re_sha256_update(&c, data, n);
    re_sha256_final(&c, out);
}
