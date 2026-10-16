// re_map.h - string keyed open addressing hash map, arena backed, no deletions.
// Module: util (C11).
// Owns: FNV-1a string hashing, insert, lookup, indexed iteration, growth at 75 pct.
// Depends: re_arena and re_str. No I/O, no globals, individual delete unsupported.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "utils/mem/re_arena.h"
#include "utils/text/re_str.h"

typedef struct {
    const char *key;
    const void *val;
    uint64_t hash;
} re_map_ent_t;

typedef struct {
    re_map_ent_t *ents;
    size_t cap; /* always a power of two */
    size_t len;
    re_arena_t *arena;
} re_map_t;

static inline uint64_t re_hash_str(const char *s, size_t n) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < n; i++) {
        h ^= (uint64_t)(uint8_t)s[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static inline void re_map_init(re_map_t *m, re_arena_t *a, size_t cap_hint) {
    m->ents = NULL;
    m->cap = 0;
    m->len = 0;
    m->arena = a;
    (void)cap_hint;
}

static inline size_t re_hash_slot(uint64_t hash, size_t cap) {
    return (size_t)hash & (cap - 1);
}

static inline bool re_map_grow(re_map_t *m, size_t want) {
    size_t cap = m->cap ? m->cap : 64;
    while (cap * 3 < want * 4)
        cap *= 2;
    re_map_ent_t *ne = (re_map_ent_t *)re_arena_calloc(m->arena, cap, sizeof(*ne));
    if (!ne)
        return false;
    for (size_t i = 0; i < m->cap; i++) {
        if (!m->ents[i].key)
            continue;
        size_t s = re_hash_slot(m->ents[i].hash, cap);
        while (ne[s].key)
            s = (s + 1) & (cap - 1);
        ne[s] = m->ents[i];
    }
    m->ents = ne;
    m->cap = cap;
    return true;
}

static inline re_map_ent_t *re_map_find(const re_map_t *m, const char *key, size_t kn,
                                        uint64_t hash) {
    if (!m->cap)
        return NULL;
    size_t s = re_hash_slot(hash, m->cap);
    for (size_t probe = 0; probe < m->cap; probe++) {
        re_map_ent_t *e = &m->ents[s];
        if (!e->key)
            return NULL;
        if (e->hash == hash && strlen(e->key) == kn && memcmp(e->key, key, kn) == 0)
            return e;
        s = (s + 1) & (m->cap - 1);
    }
    return NULL;
}

// Insert or update. The key is copied into the arena, so the caller keeps
// ownership of the pointer it passed in.
static inline bool re_map_put(re_map_t *m, const char *key, size_t kn, const void *val) {
    uint64_t hash = re_hash_str(key, kn);
    re_map_ent_t *e = re_map_find(m, key, kn, hash);
    if (e) {
        e->val = val;
        return true;
    }
    if (m->len * 4 >= m->cap * 3 && !re_map_grow(m, m->len + 1))
        return false;
    char *owned = re_arena_strndup(m->arena, key, kn);
    if (!owned)
        return false;
    size_t s = re_hash_slot(hash, m->cap);
    while (m->ents[s].key)
        s = (s + 1) & (m->cap - 1);
    m->ents[s].key = owned;
    m->ents[s].val = val;
    m->ents[s].hash = hash;
    m->len++;
    return true;
}

static inline bool re_map_puts(re_map_t *m, re_str_t key, const void *val) {
    return re_map_put(m, key.p, key.n, val);
}

static inline const void *re_map_get(const re_map_t *m, const char *key, size_t kn) {
    re_map_ent_t *e = re_map_find(m, key, kn, re_hash_str(key, kn));
    return e ? e->val : NULL;
}

static inline const void *re_map_gets(const re_map_t *m, re_str_t key) {
    return re_map_get(m, key.p, key.n);
}

static inline bool re_map_has(const re_map_t *m, const char *key, size_t kn) {
    return re_map_get(m, key, kn) != NULL;
}

// Indexed iteration skips empty slots. Dense iteration, so callers should break
// after m->len hits.
static inline const char *re_map_key_at(const re_map_t *m, size_t i) {
    return (m->ents && i < m->cap) ? m->ents[i].key : NULL;
}

static inline const void *re_map_val_at(const re_map_t *m, size_t i) {
    return (m->ents && i < m->cap) ? m->ents[i].val : NULL;
}

static inline size_t re_map_count(const re_map_t *m) {
    return m->len;
}
#ifdef __cplusplus
}
#endif
