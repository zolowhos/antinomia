// re_dbg_lock.h - session serialize lock and MCP command queue.
// Module: feature (C11).
// Owns: owner-thread queue so MCP never blocks wait_event.
// Depends: none. One process-wide lock matches the global session model.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

typedef void (*re_dbg_job_fn)(void *ctx);

// Mark the calling thread as the session owner (TUI). MCP then queues work.
void re_dbg_lock_set_owner(bool on);

// Run fn on the owner thread when owned; otherwise run inline now.
// Jobs never run while the session mutex is held (wait_event drains).
void re_dbg_lock_submit(re_dbg_job_fn fn, void *ctx);

// Owner drains queued MCP jobs. Safe to call from the TUI frame loop.
void re_dbg_lock_drain(void);

// wait_event must not hold the mutex across the OS wait. Call before/after.
void re_dbg_lock_before_wait(void);
void re_dbg_lock_after_wait(void);

// Direct acquire/release for TUI refresh around shared session reads.
void re_dbg_lock_acquire(void);
void re_dbg_lock_release(void);

#ifdef __cplusplus
}
#endif
