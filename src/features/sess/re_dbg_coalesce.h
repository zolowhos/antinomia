// re_dbg_coalesce.h - soft/BP-storm event coalescing and robust wait_event.
// Module: feature (C11).
// Owns: soft-kind tests, BP-storm merge, in-place coalesce, session wait body.
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

// Same-addr BREAKPOINT runs collapse under multi-thread BP storms.
bool re_dbg_event_is_bp_storm(const re_dbg_event_t *a, const re_dbg_event_t *b);

// Collapse same-kind soft runs and same-addr BP runs. Writes dropped; returns length.
size_t re_dbg_coalesce_events(re_dbg_event_t *evs, size_t n, uint32_t *dropped);

// Session wait with soft/BP-storm drain. Implements re_dbg_session_wait_event.
// On EXIT, clears session.attached and records exit_code (inferior exit races).
re_err_code_t re_dbg_coalesce_wait(re_dbg_session_t *s, re_dbg_event_t *ev, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
