// re_dbg_coalesce.h - soft debug-event coalescing for wait storms.
// Module: feature (C11).
// Owns: soft-kind tests, in-place coalesce, and session wait_event body.
// Depends: re_dbg.h, re_dbg_lock. Dual-OS: runs above the backend vtable.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "features/debug/re_dbg.h"

// Soft events may be merged under load; hard stops are never dropped.
bool re_dbg_event_is_soft(re_dbg_event_kind_t kind);

// Collapse same-kind soft runs in place. Writes dropped count; returns new length.
size_t re_dbg_coalesce_events(re_dbg_event_t *evs, size_t n, uint32_t *dropped);

// Session wait with soft-event drain/coalesce. Implements re_dbg_session_wait_event.
re_err_code_t re_dbg_coalesce_wait(re_dbg_session_t *s, re_dbg_event_t *ev, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
