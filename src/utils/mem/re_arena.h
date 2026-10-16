// re_arena.h - bump allocator giving one malloc and one free per command.
// Module: util (C11).
// Owns: bump-pointer blocks, chunk list, reset and free, typed dup helpers.
// Depends: libc malloc and free only. No I/O, no globals, thread-unsafe by design.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RE_ARENA_ALIGN 16u
#define RE_ARENA_DEFAULT_BLOCK (64u * 1024u)
#define RE_ARENA_MIN_BLOCK 256u

typedef struct re_arena_blk {
    struct re_arena_blk *next;
    size_t used;
    size_t cap;
} re_arena_blk_t;

typedef struct {
    re_arena_blk_t *head;
    size_t blk_size;
    size_t total;
    size_t blocks;
} re_arena_t;

static inline void re_arena_init(re_arena_t *a, size_t blk_size) {
    a->head = NULL;
    a->blk_size = blk_size < RE_ARENA_MIN_BLOCK ? RE_ARENA_DEFAULT_BLOCK : blk_size;
    a->total = 0;
    a->blocks = 0;
}

// Allocate n bytes, 16 byte aligned, or NULL. Never frees; see re_arena_reset.
static inline void *re_arena_alloc(re_arena_t *a, size_t n) {
    if (n == 0)
        n = 1;
    size_t need = RE_ARENA_ALIGN + n;
    re_arena_blk_t *b = a->head;
    if (!b || b->cap - b->used < need) {
        size_t cap = a->blk_size;
        while (cap < need)
            cap *= 2;
        b = (re_arena_blk_t *)malloc(sizeof(*b) + cap);
        if (!b)
            return NULL;
        b->next = a->head;
        b->used = 0;
        b->cap = cap;
        a->head = b;
        a->blocks++;
    }
    uintptr_t raw = (uintptr_t)((unsigned char *)b + sizeof(*b) + b->used);
    uintptr_t aligned = (raw + (RE_ARENA_ALIGN - 1)) & ~(uintptr_t)(RE_ARENA_ALIGN - 1);
    b->used = (size_t)((unsigned char *)aligned - ((unsigned char *)b + sizeof(*b))) + n;
    a->total += n;
    return (void *)aligned;
}

static inline void *re_arena_calloc(re_arena_t *a, size_t count, size_t size) {
    if (count && size > (size_t)-1 / count)
        return NULL;
    size_t n = count * size;
    void *p = re_arena_alloc(a, n);
    if (p)
        memset(p, 0, n);
    return p;
}

static inline char *re_arena_strndup(re_arena_t *a, const char *s, size_t n) {
    char *d = (char *)re_arena_alloc(a, n + 1);
    if (!d)
        return NULL;
    if (n)
        memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

static inline char *re_arena_strdup(re_arena_t *a, const char *s) {
    return re_arena_strndup(a, s, strlen(s));
}

static inline void *re_arena_memdup(re_arena_t *a, const void *s, size_t n) {
    void *d = re_arena_alloc(a, n);
    if (d && n)
        memcpy(d, s, n);
    return d;
}

// Keep the newest block for reuse, free the rest. O(1) amortized for the common
// case of a command that allocates from one block.
static inline void re_arena_reset(re_arena_t *a) {
    re_arena_blk_t *b = a->head;
    a->total = 0;
    if (!b) {
        a->blocks = 0;
        return;
    }
    re_arena_blk_t *n = b->next;
    while (n) {
        re_arena_blk_t *nx = n->next;
        free(n);
        n = nx;
        a->blocks--;
    }
    b->next = NULL;
    b->used = 0;
}

static inline void re_arena_free(re_arena_t *a) {
    re_arena_blk_t *b = a->head;
    while (b) {
        re_arena_blk_t *n = b->next;
        free(b);
        b = n;
    }
    a->head = NULL;
    a->total = 0;
    a->blocks = 0;
}
#ifdef __cplusplus
}
#endif
