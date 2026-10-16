// re_disasm_cache.c - small linear disasm window with write invalidation.
// Module: feature (C11).
// Owns: cache clear/invalidate/fill helpers used by the debug session.
// Depends: re_disasm.h. No OS calls; callers supply the bytes.
#include "features/disasm/re_disasm.h"

#include <string.h>

void re_disasm_cache_clear(re_disasm_cache_t *c) {
    if (!c)
        return;
    memset(c, 0, sizeof(*c));
}

void re_disasm_cache_invalidate(re_disasm_cache_t *c, uint64_t addr, size_t n) {
    uint64_t end;
    uint64_t cend;
    if (!c || !c->valid)
        return;
    if (n == 0) {
        re_disasm_cache_clear(c);
        return;
    }
    end = addr + (uint64_t)n;
    cend = c->base + (uint64_t)c->span;
    if (addr < cend && end > c->base)
        re_disasm_cache_clear(c);
}

re_err_code_t re_disasm_cache_fill(re_disasm_cache_t *c, uint64_t addr, const uint8_t *buf,
                                   size_t n, size_t max_n) {
    size_t got = 0;
    re_err_code_t rc;
    if (!c || !buf)
        return RE_E_USAGE;
    if (max_n == 0)
        max_n = RE_DISASM_CACHE_N;
    if (max_n > RE_DISASM_CACHE_N)
        max_n = RE_DISASM_CACHE_N;
    if (c->valid && c->base == addr && c->span == n && c->n <= max_n)
        return RE_OK;
    rc = re_disasm_x64_many(addr, buf, n, max_n, c->lines, &got);
    if (rc != RE_OK)
        return rc;
    {
        size_t i;
        for (i = 0; i < got; i++)
            re_disasm_enrich(&c->lines[i]);
    }
    c->valid = true;
    c->base = addr;
    c->span = n;
    c->n = got;
    return RE_OK;
}
