// re_seed.h - deterministic PRNG for analysis reproducibility (--seed).
// Module: util (C11).
// Owns: seeded LCG and index shuffle used by local analysis helpers.
// Depends: stdint/stddef only. No I/O; no global RNG state.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

typedef struct re_rng {
    uint64_t state;
} re_rng_t;

/* Seed with a non-zero mixer so seed 0 is still usable and stable. */
void re_rng_init(re_rng_t *r, uint64_t seed);

/* Next 32-bit value in [0, UINT32_MAX]. */
uint32_t re_rng_u32(re_rng_t *r);

/* Fisher-Yates shuffle of a uint32_t index array (n elements). */
void re_rng_shuffle_u32(re_rng_t *r, uint32_t *idx, size_t n);

#ifdef __cplusplus
}
#endif
