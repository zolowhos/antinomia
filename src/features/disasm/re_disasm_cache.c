// re_disasm_cache.c - multi-slot disasm windows with byte-aware hits.
// Module: feature (C11).
// Owns: cache clear/invalidate/fill helpers used by the debug session.
// Depends: re_disasm.h. No OS calls; callers supply the bytes.
#include "features/disasm/re_disasm.h"

#include <string.h>

static void mirror_slot(re_disasm_cache_t *c, const re_disasm_slot_t *s) {
    size_t i;
    if (!c || !s) {
        return;
    }
    c->valid = s->valid;
    c->base = s->base;
    c->span = s->span;
    c->n = s->n;
    for (i = 0; i < s->n && i < RE_DISASM_CACHE_N; i++) {
        c->lines[i] = s->lines[i];
    }
}

static bool bytes_eq(const uint8_t *a, const uint8_t *b, size_t n) {
    size_t i;
    if (!a || !b) {
        return false;
    }
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

static re_disasm_slot_t *find_slot(re_disasm_cache_t *c, uint64_t addr, size_t span) {
    size_t i;
    for (i = 0; i < RE_DISASM_CACHE_SLOTS; i++) {
        re_disasm_slot_t *s = &c->slots[i];
        if (s->valid && s->base == addr && s->span == span) {
            return s;
        }
    }
    return NULL;
}

static re_disasm_slot_t *lru_slot(re_disasm_cache_t *c) {
    size_t i;
    size_t best = 0;
    uint32_t best_used = c->slots[0].used;
    for (i = 1; i < RE_DISASM_CACHE_SLOTS; i++) {
        if (!c->slots[i].valid) {
            return &c->slots[i];
        }
        if (c->slots[i].used < best_used) {
            best_used = c->slots[i].used;
            best = i;
        }
    }
    if (!c->slots[0].valid) {
        return &c->slots[0];
    }
    return &c->slots[best];
}

void re_disasm_cache_clear(re_disasm_cache_t *c) {
    if (!c) {
        return;
    }
    memset(c, 0, sizeof(*c));
}

void re_disasm_cache_invalidate(re_disasm_cache_t *c, uint64_t addr, size_t n) {
    uint64_t end;
    size_t i;
    bool any = false;
    if (!c) {
        return;
    }
    if (n == 0) {
        re_disasm_cache_clear(c);
        return;
    }
    end = addr + (uint64_t)n;
    for (i = 0; i < RE_DISASM_CACHE_SLOTS; i++) {
        re_disasm_slot_t *s = &c->slots[i];
        uint64_t cend;
        if (!s->valid) {
            continue;
        }
        cend = s->base + (uint64_t)s->span;
        if (addr < cend && end > s->base) {
            memset(s, 0, sizeof(*s));
            c->invalidations++;
            any = true;
        }
    }
    if (any) {
        c->valid = false;
        c->base = 0;
        c->span = 0;
        c->n = 0;
    }
}

re_err_code_t re_disasm_cache_fill(re_disasm_cache_t *c, uint64_t addr, const uint8_t *buf,
                                   size_t n, size_t max_n) {
    re_disasm_slot_t *s;
    size_t got = 0;
    size_t store;
    size_t i;
    re_err_code_t rc;
    if (!c || !buf) {
        return RE_E_USAGE;
    }
    if (max_n == 0) {
        max_n = RE_DISASM_CACHE_N;
    }
    if (max_n > RE_DISASM_CACHE_N) {
        max_n = RE_DISASM_CACHE_N;
    }
    store = n;
    if (store > RE_DISASM_CACHE_BYTES) {
        store = RE_DISASM_CACHE_BYTES;
    }
    s = find_slot(c, addr, store);
    if (s && s->n <= max_n && bytes_eq(s->raw, buf, store)) {
        c->clock++;
        s->used = c->clock;
        c->hits++;
        mirror_slot(c, s);
        return RE_OK;
    }
    if (!s) {
        s = lru_slot(c);
    }
    rc = re_disasm_x64_many(addr, buf, n, max_n, s->lines, &got);
    if (rc != RE_OK) {
        return rc;
    }
    for (i = 0; i < got; i++) {
        re_disasm_enrich(&s->lines[i]);
    }
    s->valid = true;
    s->base = addr;
    s->span = store;
    s->n = got;
    memcpy(s->raw, buf, store);
    if (store < RE_DISASM_CACHE_BYTES) {
        memset(s->raw + store, 0, RE_DISASM_CACHE_BYTES - store);
    }
    c->clock++;
    s->used = c->clock;
    c->misses++;
    mirror_slot(c, s);
    return RE_OK;
}

void re_disasm_cache_stats(const re_disasm_cache_t *c, uint32_t *hits, uint32_t *misses,
                           uint32_t *invalidations) {
    if (hits) {
        *hits = c ? c->hits : 0;
    }
    if (misses) {
        *misses = c ? c->misses : 0;
    }
    if (invalidations) {
        *invalidations = c ? c->invalidations : 0;
    }
}
