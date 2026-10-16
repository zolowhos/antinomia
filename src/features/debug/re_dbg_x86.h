// re_dbg_x86.h - minimal x86_64 helpers for step-over call detection.
// Module: feature (C11).
// Owns: call-instruction length at a code address (no Capstone dependency).
// Depends: none beyond stdint. Used by session step-over.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// If bytes at rip are a CALL, write instruction length to *len_out and return true.
// Cap is bytes available in buf (need at least 15 for worst-case decode).
bool re_dbg_x86_is_call(const uint8_t *buf, size_t cap, size_t *len_out);

#ifdef __cplusplus
}
#endif
