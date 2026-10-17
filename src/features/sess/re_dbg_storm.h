// re_dbg_storm.h - wait-loop drain helpers for soft and BP-storm races.
// Module: feature (C11).
// Owns: non-blocking peek, soft drain, same-addr BP peer resume.
// Depends: re_dbg.h. Used only by re_dbg_coalesce.c.
#pragma once

#include "features/debug/re_dbg.h"

#include <stdbool.h>
#include <stdint.h>

/* Non-blocking peek (1 ms). Soft drains may release a pending Win event. */
re_err_code_t re_dbg_storm_peek(re_dbg_session_t *s, re_dbg_event_t *nxt, bool release_pending);

re_err_code_t re_dbg_storm_drain_soft(re_dbg_session_t *s, re_dbg_event_t *ev, uint32_t *dropped);

/* Collapse same-addr multi-thread BP hits; resume peers without stealing rearm. */
re_err_code_t re_dbg_storm_drain_bp(re_dbg_session_t *s, re_dbg_event_t *ev, uint32_t *dropped);

/* Shared post-process: BP hit, cond/fast-resume, rearm STEP, exception filter. */
bool re_dbg_storm_post(re_dbg_session_t *s, re_dbg_event_t *ev);

bool re_dbg_storm_load_bp_stop(re_dbg_session_t *s, re_dbg_event_t *ev);

void re_dbg_storm_merge_count(re_dbg_event_t *dst, const re_dbg_event_t *src, uint32_t *dropped);
