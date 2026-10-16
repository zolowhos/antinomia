// re_ai.h - local deterministic AI-oriented helper API (no cloud).
// Module: feature/ai (C11).
// Owns: explain-insn, suggest-BP, summarize-exception result types and entry points.
// Depends: re_dbg, re_disasm. Helpers stay offline; callers emit antinomia/1 JSON.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "features/debug/re_dbg.h"
#include "features/disasm/re_disasm.h"
#include "utils/sys/re_err.h"

#define RE_AI_SUMMARY_MAX 160
#define RE_AI_DETAIL_MAX 256
#define RE_AI_ADVICE_MAX 128
#define RE_AI_QUERY_MAX 96
#define RE_AI_REASON_MAX 96
#define RE_AI_NAME_MAX 32
#define RE_AI_BP_MAX 24

typedef struct {
    char kind[16]; /* call, jmp, jcc, ret, mem, other */
    char summary[RE_AI_SUMMARY_MAX];
    char detail[RE_AI_DETAIL_MAX];
} re_ai_explain_t;

typedef struct {
    char query[RE_AI_QUERY_MAX]; /* symbol or module:symbol for bp */
    char reason[RE_AI_REASON_MAX];
    uint64_t addr;
    bool resolved;
} re_ai_bp_site_t;

typedef struct {
    int32_t code;
    uint64_t addr;
    bool first_chance;
    bool from_event;
    char name[RE_AI_NAME_MAX];
    char summary[RE_AI_SUMMARY_MAX];
    char advice[RE_AI_ADVICE_MAX];
} re_ai_exsum_t;

/* Fill explain from a decoded insn (flags/text already enriched). */
re_err_code_t re_ai_explain_insn(const re_disasm_insn_t *in, re_ai_explain_t *out);

/* Suggest BP sites on interesting imports; resolve via the live session. */
re_err_code_t re_ai_suggest_bp(re_dbg_session_t *s, re_ai_bp_site_t *out, size_t cap,
                               size_t *n_out);

/* Summarize last EXCEPTION event, or a bare code when ev is NULL / not EXCEPTION. */
re_err_code_t re_ai_summarize_exception(const re_dbg_event_t *ev, int32_t code_override,
                                        bool have_code, re_ai_exsum_t *out);

#ifdef __cplusplus
}
#endif
