// re_seed.c - splitmix64-ish LCG for deterministic analysis shuffles.
// Module: util (C11).
// Owns: re_rng_* implementations. No heap; no I/O.
// Depends: re_seed.h only.
#include "utils/algo/re_seed.h"

void re_rng_init(re_rng_t *r, uint64_t seed) {
    if (!r)
        return;
    /* Mix so seed 0 is distinct from an uninitialised zero state path. */
    r->state = seed + 0x9e3779b97f4a7c15ull;
}

uint32_t re_rng_u32(re_rng_t *r) {
    uint64_t z;
    if (!r)
        return 0;
    z = (r->state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    z = z ^ (z >> 31);
    return (uint32_t)z;
}

void re_rng_shuffle_u32(re_rng_t *r, uint32_t *idx, size_t n) {
    size_t i;
    if (!r || !idx || n < 2)
        return;
    for (i = n - 1; i > 0; i--) {
        uint32_t j = re_rng_u32(r) % (uint32_t)(i + 1u);
        uint32_t tmp = idx[i];
        idx[i] = idx[j];
        idx[j] = tmp;
    }
}
