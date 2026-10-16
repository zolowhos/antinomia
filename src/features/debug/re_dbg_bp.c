// re_dbg_bp.c - plant and restore software breakpoints via backend mem ops.
// Module: feature (C11).
// Owns: int3 (0xCC) set/clear used by headless integration until Pack D lands.
// Depends: re_dbg_bp.h. Backend must implement read_mem and write_mem.
#include "features/debug/re_dbg_bp.h"

#include <string.h>

re_err_code_t re_dbg_swbp_set(re_dbg_backend_t *b, uint64_t addr, re_dbg_swbp_t *bp) {
    uint8_t cur = 0;
    uint8_t cc = 0xCCu;
    re_err_code_t c;
    if (!b || !b->vt || !bp)
        return RE_E_USAGE;
    if (!b->vt->read_mem || !b->vt->write_mem)
        return RE_E_UNSUPPORTED;
    memset(bp, 0, sizeof(*bp));
    c = b->vt->read_mem(b, addr, &cur, 1);
    if (c != RE_OK)
        return c;
    c = b->vt->write_mem(b, addr, &cc, 1);
    if (c != RE_OK)
        return c;
    bp->addr = addr;
    bp->orig = cur;
    bp->active = true;
    return RE_OK;
}

re_err_code_t re_dbg_swbp_clear(re_dbg_backend_t *b, re_dbg_swbp_t *bp) {
    re_err_code_t c;
    if (!b || !b->vt || !bp)
        return RE_E_USAGE;
    if (!bp->active)
        return RE_OK;
    if (!b->vt->write_mem)
        return RE_E_UNSUPPORTED;
    c = b->vt->write_mem(b, bp->addr, &bp->orig, 1);
    if (c != RE_OK)
        return c;
    bp->active = false;
    return RE_OK;
}
