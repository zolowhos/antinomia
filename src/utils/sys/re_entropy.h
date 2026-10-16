// re_entropy.h - Shannon entropy over a span, for packer detection per section.
// Module: util (C11).
// Owns: one histogram pass and the log2 reduction, plus a counts based variant.
// Depends: math.h only. No tables, no allocation, no globals.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "utils/mem/re_buf.h"

// Returns bits per byte, 0.0 for an empty span and exactly 8.0 for uniform noise.
// A section above about 7.0 is compressed or encrypted, which for a driver means
// either packing or an embedded compressed resource.
static inline double re_entropy(re_span_t s) {
    if (s.n == 0)
        return 0.0;
    size_t counts[256];
    for (int i = 0; i < 256; i++)
        counts[i] = 0;
    for (size_t i = 0; i < s.n; i++)
        counts[s.p[i]]++;
    double h = 0.0;
    double n = (double)s.n;
    for (int i = 0; i < 256; i++) {
        if (counts[i]) {
            double p = (double)counts[i] / n;
            h -= p * log2(p);
        }
    }
    return h;
}

// Shannon entropy of a byte distribution, reused by the capability rules to score
// how concentrated a set of constants is. Zero buckets are skipped.
static inline double re_entropy_of_counts(const size_t *counts, size_t n) {
    size_t total = 0;
    for (size_t i = 0; i < n; i++)
        total += counts[i];
    if (!total)
        return 0.0;
    double h = 0.0;
    for (size_t i = 0; i < n; i++) {
        if (counts[i]) {
            double p = (double)counts[i] / (double)total;
            h -= p * log2(p);
        }
    }
    return h;
}
#ifdef __cplusplus
}
#endif
