// re_dbg_bp.h - software breakpoints and exception/signal policy.
// Module: feature (C11).
// Owns: int3 plant/restore/re-arm, x86 CALL length, exception ignore/pass rules.
// Depends: re_dbg.h. Backends supply mem and regs through the vtable.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "features/debug/re_dbg.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RE_DBG_MAX_BP 64
#define RE_DBG_MAX_EX 32
#define RE_DBG_BP_EXPR_MAX 96

typedef struct {
    uint64_t addr;
    uint8_t orig;
    bool used;
    bool enabled;
    bool armed;
    bool temporary;
    bool fast_resume;
    uint32_t hit_count;
    char break_cond[RE_DBG_BP_EXPR_MAX];
    char log_text[RE_DBG_BP_EXPR_MAX];
    char log_cond[RE_DBG_BP_EXPR_MAX];
    char cmd[RE_DBG_BP_EXPR_MAX];
    char cmd_cond[RE_DBG_BP_EXPR_MAX];
} re_dbg_bp_slot_t;

struct re_dbg_bps {
    re_dbg_bp_slot_t slot[RE_DBG_MAX_BP];
    bool need_rearm;
    uint64_t rearm_addr;
    bool rearm_then_cont;
    bool step_over_active;
    re_dbg_ex_rule_t ex[RE_DBG_MAX_EX];
    re_dbg_ex_policy_t pending_policy;
    int32_t pending_code;
    bool pending_valid;
};

void re_dbg_bps_clear(re_dbg_bps_t *bps);
re_dbg_bp_slot_t *re_dbg_bp_find(re_dbg_bps_t *bps, uint64_t addr);
re_err_code_t re_dbg_bp_set(re_dbg_bps_t *bps, re_dbg_backend_t *b, uint64_t addr, bool temporary);
re_err_code_t re_dbg_bp_clear(re_dbg_bps_t *bps, re_dbg_backend_t *b, uint64_t addr);
re_err_code_t re_dbg_bp_enable(re_dbg_bps_t *bps, re_dbg_backend_t *b, uint64_t addr);
re_err_code_t re_dbg_bp_disable(re_dbg_bps_t *bps, re_dbg_backend_t *b, uint64_t addr);
re_err_code_t re_dbg_bp_list(const re_dbg_bps_t *bps, re_dbg_bp_info_t *out, size_t cap,
                             size_t *n_out);
re_err_code_t re_dbg_bp_set_field(re_dbg_bps_t *bps, uint64_t addr, const char *field,
                                  const char *value);

/* On BREAK: if int3 was ours, restore orig, rewind RIP, mark re-arm. */
bool re_dbg_bp_on_hit(re_dbg_bps_t *bps, re_dbg_backend_t *b, uint32_t tid, uint64_t *addr_inout);

/* Before cont: single-step past a restored BP, then re-plant if still enabled. */
re_err_code_t re_dbg_bp_prepare_cont(re_dbg_bps_t *bps, re_dbg_backend_t *b);

/* After a STEP event: re-plant pending BP; optionally continue for rearm_then_cont. */
re_err_code_t re_dbg_bp_after_step(re_dbg_bps_t *bps, re_dbg_backend_t *b, bool *consumed);

/* Restore all armed bytes before detach (leaves slots intact unless wipe). */
re_err_code_t re_dbg_bp_disarm_all(re_dbg_bps_t *bps, re_dbg_backend_t *b);

re_err_code_t re_dbg_ex_set(re_dbg_bps_t *bps, int32_t lo, int32_t hi, re_dbg_ex_policy_t policy,
                            bool first_chance_only);
re_err_code_t re_dbg_ex_clear(re_dbg_bps_t *bps, int32_t lo, int32_t hi);
re_err_code_t re_dbg_ex_list(const re_dbg_bps_t *bps, re_dbg_ex_rule_t *out, size_t cap,
                             size_t *n_out);
re_dbg_ex_policy_t re_dbg_ex_lookup(const re_dbg_bps_t *bps, int32_t code, bool first_chance);

/* Apply policy to an EXCEPTION event. Returns true if the event was consumed
 * (auto-continued); false if the caller should surface it. Sets pending_* for cont. */
bool re_dbg_ex_filter(re_dbg_bps_t *bps, re_dbg_backend_t *b, re_dbg_event_t *ev);

/* Arm backend set_pass from pending exception policy before a user continue. */
re_err_code_t re_dbg_ex_prepare_cont(re_dbg_bps_t *bps, re_dbg_backend_t *b);

#ifdef __cplusplus
}
#endif

/* Minimal x86_64 helpers for step-over (CALL length). */
bool re_dbg_x86_is_call(const uint8_t *buf, size_t cap, size_t *len_out);
