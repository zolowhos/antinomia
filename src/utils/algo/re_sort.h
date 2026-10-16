// re_sort.h - shared introsort over a raw byte buffer, so every container sorts alike.
// Module: util (C11).
// Owns: re_sort_ entry point and the re_cmp_fn contract used by all containers.
// Depends: none. No I/O, no globals, not stable, three way pivot.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

// Compare two elements. Return <0, 0 or >0. ctx is the caller's opaque cookie.
typedef int (*re_cmp_fn)(const void *a, const void *b, void *ctx);

// Sort n elements of esz bytes at base, in place. No allocation, no recursion
// deeper than log2(n), so it is safe on adversarial element counts.
void re_sort_(void *base, size_t n, size_t esz, re_cmp_fn cmp, void *ctx);

#ifdef __cplusplus
}
#endif
