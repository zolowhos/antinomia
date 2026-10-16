// re_dbg_trace.h - instruction-stream + register trace recording v1.
// Module: feature (C11).
// Owns: ring buffer, record/query, and save/load of antinomia trace files.
// Depends: re_dbg session regs/mem/disasm. Fixed cap; no heap outside arenas.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "features/debug/re_dbg.h"

#define RE_TRACE_CAP 1024u
#define RE_TRACE_TEXT_MAX 64u
#define RE_TRACE_BYTES_MAX 15u

typedef struct {
    uint64_t index; /* monotonic absolute index */
    uint32_t tid;
    uint8_t size;
    uint8_t bytes[RE_TRACE_BYTES_MAX];
    char text[RE_TRACE_TEXT_MAX];
    re_dbg_regs_t regs;
} re_dbg_trace_frame_t;

struct re_dbg_trace {
    bool recording;
    size_t count; /* frames currently stored (<= RE_TRACE_CAP) */
    size_t head;  /* next write slot in the ring */
    uint64_t next_index;
    uint64_t dropped; /* frames overwritten when the ring wraps */
    re_dbg_trace_frame_t frames[RE_TRACE_CAP];
};

void re_dbg_trace_clear(re_dbg_trace_t *t);

/* Session-facing API (CLI/MCP/TUI). Trace lives on the session arena. */
re_err_code_t re_dbg_session_trace_start(re_dbg_session_t *s);
re_err_code_t re_dbg_session_trace_stop(re_dbg_session_t *s);
re_err_code_t re_dbg_session_trace_clear(re_dbg_session_t *s);
bool re_dbg_session_trace_recording(const re_dbg_session_t *s);
size_t re_dbg_session_trace_count(const re_dbg_session_t *s);

/* Capture one frame at the current stop (RIP + insn + regs). No-op if idle. */
re_err_code_t re_dbg_session_trace_capture(re_dbg_session_t *s);

/* Single-step N times, recording each stop. Leaves recording as it was. */
re_err_code_t re_dbg_session_trace_step(re_dbg_session_t *s, uint32_t n);

/* Paged query: skip `offset` oldest frames, return up to `limit`. */
re_err_code_t re_dbg_session_trace_query(re_dbg_session_t *s, size_t offset, size_t limit,
                                         re_dbg_trace_frame_t *out, size_t cap, size_t *n_out,
                                         size_t *total_out, bool *truncated);

re_err_code_t re_dbg_session_trace_save(re_dbg_session_t *s, const char *path);
re_err_code_t re_dbg_session_trace_load(re_dbg_session_t *s, const char *path);

/* Offline MCP replay: load a Phase 2 antinomia/1 trace file without a live target. */
re_err_code_t re_dbg_session_replay_open(re_dbg_session_t *s, const char *path);
re_err_code_t re_dbg_session_replay_close(re_dbg_session_t *s);
bool re_dbg_session_replay_active(const re_dbg_session_t *s);
const char *re_dbg_session_replay_path(const re_dbg_session_t *s);
/* Resolve one frame by absolute trace index (not ring offset). */
re_err_code_t re_dbg_session_replay_frame(re_dbg_session_t *s, uint64_t index,
                                          re_dbg_trace_frame_t *out);

#ifdef __cplusplus
}
#endif
