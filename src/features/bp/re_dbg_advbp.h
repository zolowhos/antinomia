// re_dbg_advbp.h - hardware, memory, and module-load breakpoint API.
// Module: feature (C11).
// Owns: session wrappers and table ops for Phase 2 advanced breakpoints.
// Depends: re_dbg.h, re_dbg_bp.h. OS debug regs stay behind the backend vtable.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "features/debug/re_dbg.h"
#include "features/debug/re_dbg_bp.h"

#include <stddef.h>
#include <stdint.h>

#define RE_DBG_MAX_HW 4
#define RE_DBG_MAX_MEM_BP 16
#define RE_DBG_MAX_LOAD_BP 8

typedef enum {
    RE_DBG_BP_KIND_SOFT = 0,
    RE_DBG_BP_KIND_HW,
    RE_DBG_BP_KIND_MEM,
    RE_DBG_BP_KIND_LOAD
} re_dbg_bp_kind_t;

/* x86 DR7 R/W field: 0=exec, 1=write, 3=read/write. */
typedef enum { RE_DBG_BP_AX_X = 0, RE_DBG_BP_AX_W = 1, RE_DBG_BP_AX_RW = 3 } re_dbg_bp_access_t;

typedef struct {
    uint64_t addr;
    uint8_t orig;
    bool enabled;
    bool temporary;
    re_dbg_bp_kind_t kind;
    uint8_t access;
    uint8_t size;
    size_t len;
    char name[128];
} re_dbg_bp_ex_t;

typedef struct {
    bool used;
    bool enabled;
    uint64_t addr;
    uint8_t size;
    uint8_t access;
    int slot;
} re_dbg_hw_slot_t;

typedef struct {
    bool used;
    bool enabled;
    uint64_t addr;
    size_t len;
    uint8_t access;
    int hw_slot;
} re_dbg_mem_slot_t;

typedef struct {
    bool used;
    bool enabled;
    char name[128];
} re_dbg_load_slot_t;

/* Extra tables live beside the soft-BP table (session-owned). */
struct re_dbg_advbp {
    re_dbg_hw_slot_t hw[RE_DBG_MAX_HW];
    re_dbg_mem_slot_t mem[RE_DBG_MAX_MEM_BP];
    re_dbg_load_slot_t load[RE_DBG_MAX_LOAD_BP];
    uint64_t rendezvous_addr;
    bool rendezvous_armed;
};

void re_dbg_advbp_clear(re_dbg_advbp_t *adv);
re_err_code_t re_dbg_advbp_disarm_all(re_dbg_advbp_t *adv, re_dbg_backend_t *b);

re_err_code_t re_dbg_session_caps(re_dbg_session_t *s, re_dbg_caps_t *out);
re_err_code_t re_dbg_session_hw_bp_set(re_dbg_session_t *s, uint64_t addr, uint8_t size,
                                       re_dbg_bp_access_t access);
re_err_code_t re_dbg_session_hw_bp_clear(re_dbg_session_t *s, uint64_t addr);
re_err_code_t re_dbg_session_mem_bp_set(re_dbg_session_t *s, uint64_t addr, size_t len,
                                        re_dbg_bp_access_t access);
re_err_code_t re_dbg_session_mem_bp_clear(re_dbg_session_t *s, uint64_t addr);
re_err_code_t re_dbg_session_load_bp_set(re_dbg_session_t *s, const char *name);
re_err_code_t re_dbg_session_load_bp_clear(re_dbg_session_t *s, const char *name);
re_err_code_t re_dbg_session_bp_list_ex(re_dbg_session_t *s, re_dbg_bp_ex_t *out, size_t cap,
                                        size_t *n_out);

/* After a BREAK: resolve HW/mem hits; after LOAD_MODULE: match bpdll filters. */
bool re_dbg_advbp_on_break(re_dbg_advbp_t *adv, re_dbg_backend_t *b, uint32_t tid,
                           uint64_t *addr_inout);
bool re_dbg_advbp_load_matches(const re_dbg_advbp_t *adv, const char *path);
bool re_dbg_advbp_has_load(const re_dbg_advbp_t *adv);
bool re_dbg_advbp_is_rendezvous(const re_dbg_advbp_t *adv, uint64_t addr);
re_err_code_t re_dbg_advbp_arm_rendezvous(re_dbg_advbp_t *adv, re_dbg_backend_t *b,
                                          re_dbg_bps_t *soft);

const char *re_dbg_bp_kind_str(re_dbg_bp_kind_t kind);
const char *re_dbg_bp_access_str(re_dbg_bp_access_t access);

#ifdef __cplusplus
}
#endif
