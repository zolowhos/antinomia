// re_hash.h - MD5 and SHA-256 for file identity and Lumina style metadata lookups.
// Module: util (C11).
// Owns: streaming MD5 and SHA-256 state plus one shot helpers.
// Depends: none. No I/O, no allocation, digests are written into caller buffers.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#define RE_MD5_LEN 16
#define RE_SHA256_LEN 32

typedef struct {
    uint32_t s[4];
    uint64_t bits;
    uint8_t buf[64];
    size_t n;
} re_md5_t;

typedef struct {
    uint32_t s[8];
    uint64_t bits;
    uint8_t buf[64];
    size_t n;
} re_sha256_t;

void re_md5_init(re_md5_t *c);
void re_md5_update(re_md5_t *c, const void *data, size_t n);
void re_md5_final(re_md5_t *c, uint8_t out[RE_MD5_LEN]);
void re_md5(const void *data, size_t n, uint8_t out[RE_MD5_LEN]);

void re_sha256_init(re_sha256_t *c);
void re_sha256_update(re_sha256_t *c, const void *data, size_t n);
void re_sha256_final(re_sha256_t *c, uint8_t out[RE_SHA256_LEN]);
void re_sha256(const void *data, size_t n, uint8_t out[RE_SHA256_LEN]);

#ifdef __cplusplus
}
#endif
