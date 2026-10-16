// re_dbg_bp.h - thin software breakpoint helpers over backend mem R/W.
// Module: feature (C11).
// Owns: int3 plant/restore for integration and Pack D until a richer BP table.
// Depends: re_dbg backend vtable only. No OS headers.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "features/debug/re_dbg.h"

typedef struct {
    uint64_t addr;
    uint8_t orig;
    bool active;
} re_dbg_swbp_t;

// Plant 0xCC at addr; stores the replaced byte in bp->orig.
re_err_code_t re_dbg_swbp_set(re_dbg_backend_t *b, uint64_t addr, re_dbg_swbp_t *bp);

// Restore the original byte when bp->active.
re_err_code_t re_dbg_swbp_clear(re_dbg_backend_t *b, re_dbg_swbp_t *bp);

#ifdef __cplusplus
}
#endif
