// re_set.h - pointer set, defined as a map that ignores its values.
// Module: util (C11).
// Owns: nothing new. Reuses re_map so there is exactly one hash table in the tree.
// Depends: re_map only. No I/O, no globals, individual delete unsupported.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>

#include "utils/mem/re_arena.h"
#include "utils/mem/re_map.h"
#include "utils/text/re_str.h"

typedef re_map_t re_set_t;

static inline void re_set_init(re_set_t *s, re_arena_t *a, size_t cap_hint) {
    re_map_init(s, a, cap_hint);
}

static inline bool re_set_add(re_set_t *s, const char *key, size_t kn, const void *member) {
    return re_map_put(s, key, kn, member);
}

static inline bool re_set_adds(re_set_t *s, re_str_t key, const void *member) {
    return re_map_put(s, key.p, key.n, member);
}

static inline bool re_set_has(const re_set_t *s, const char *key, size_t kn) {
    return re_map_has(s, key, kn);
}

static inline bool re_set_hass(const re_set_t *s, re_str_t key) {
    return re_map_has(s, key.p, key.n);
}

static inline const void *re_set_get(const re_set_t *s, const char *key, size_t kn) {
    return re_map_get(s, key, kn);
}

static inline size_t re_set_count(const re_set_t *s) {
    return re_map_count(s);
}

// Deletion is intentionally absent. The arena owns every key, so removing one
// entry mid command would leave a hole that every iterator must remember to
// skip. Call re_arena_reset instead and rebuild.
static inline bool re_set_del(re_set_t *s, const char *key, size_t kn) {
    (void)s;
    (void)key;
    (void)kn;
    return false;
}
#ifdef __cplusplus
}
#endif
