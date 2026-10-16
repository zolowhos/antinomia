// re_dbg.h - opaque debug session and OS backend vtable.
// Module: feature (C11).
// Owns: session lifetime, backend selection, and the debug API surface.
// Depends: re_arena, re_err. No OS headers leak past the backend .c files.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "features/disasm/re_disasm.h"
#include "features/patch/re_patch.h"
#include "features/persist/re_dbg_db.h"
#include "utils/mem/re_arena.h"
#include "utils/sys/re_err.h"

/* Unified event model (roadmap): breakpoint, step, exception, load, unload,
 * thread create/exit, output, exit. CREATE is process birth; BREAK aliases BREAKPOINT. */
typedef enum {
    RE_DBG_EV_NONE = 0,
    RE_DBG_EV_CREATE,
    RE_DBG_EV_EXIT,
    RE_DBG_EV_BREAKPOINT,
    RE_DBG_EV_STEP,
    RE_DBG_EV_EXCEPTION,
    RE_DBG_EV_LOAD_MODULE,
    RE_DBG_EV_UNLOAD_MODULE,
    RE_DBG_EV_THREAD_CREATE,
    RE_DBG_EV_THREAD_EXIT,
    RE_DBG_EV_OUTPUT
} re_dbg_event_kind_t;

#define RE_DBG_EV_BREAK RE_DBG_EV_BREAKPOINT
#define RE_DBG_EV_UNLOAD RE_DBG_EV_UNLOAD_MODULE
#define RE_DBG_EV_THREAD RE_DBG_EV_THREAD_CREATE

typedef struct {
    re_dbg_event_kind_t kind;
    uint32_t pid;
    uint32_t tid;
    uint64_t addr;
    int32_t code;
    bool first_chance; /* Win first-chance; Linux signal stops are first-chance */
} re_dbg_event_t;

/* Backend capability bits: what this OS backend actually supports. */
enum {
    RE_DBG_CAP_SW_BP = 1u << 0,
    RE_DBG_CAP_HW_BP = 1u << 1,
    RE_DBG_CAP_MEM_BP = 1u << 2,
    RE_DBG_CAP_STEP = 1u << 3,
    RE_DBG_CAP_PAUSE = 1u << 4,
    RE_DBG_CAP_ATTACH = 1u << 5,
    RE_DBG_CAP_MODULES = 1u << 6,
    RE_DBG_CAP_THREADS = 1u << 7,
    RE_DBG_CAP_EXCEPTION_BP = 1u << 8,
    RE_DBG_CAP_SIGNAL_PASS = 1u << 9,   /* Linux: PTRACE_CONT(sig) */
    RE_DBG_CAP_FIRST_CHANCE = 1u << 10, /* Win: first- vs second-chance */
    RE_DBG_CAP_LOAD_BP = 1u << 11       /* module-load BP (bpdll / LOAD_DLL) */
};

typedef struct {
    uint32_t bits;
    uint32_t hw_slots; /* DR0-DR3 when RE_DBG_CAP_HW_BP; else 0 */
} re_dbg_caps_t;

/* Exception/signal policy: break (stop), pass to inferior, or ignore/swallow. */
typedef enum {
    RE_DBG_EX_BREAK_PASS = 0, /* stop; on cont deliver to inferior */
    RE_DBG_EX_BREAK_HANDLE,   /* stop; on cont swallow */
    RE_DBG_EX_PASS,           /* do not stop; deliver */
    RE_DBG_EX_IGNORE          /* do not stop; swallow */
} re_dbg_ex_policy_t;

typedef struct {
    int32_t code_lo; /* inclusive; Win exception code or Linux signo */
    int32_t code_hi;
    re_dbg_ex_policy_t policy;
    bool first_chance_only; /* if set, second-chance always breaks */
    bool used;
} re_dbg_ex_rule_t;

typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, rflags;
} re_dbg_regs_t;

typedef struct {
    uint32_t tid;
    uint64_t rip;
    char name[64];
    bool suspended;
} re_dbg_thread_t;

typedef struct {
    uint64_t base;
    uint64_t size;
    char path[260];
    char build_id[48]; /* GNU build-id hex (Linux); empty when unknown */
} re_dbg_module_t;

/* Memory map region: protect bits and coarse type (image/private/mapped/...). */
enum { RE_DBG_PROT_R = 1u << 0, RE_DBG_PROT_W = 1u << 1, RE_DBG_PROT_X = 1u << 2 };

typedef enum {
    RE_DBG_MAP_OTHER = 0,
    RE_DBG_MAP_IMAGE,
    RE_DBG_MAP_MAPPED,
    RE_DBG_MAP_PRIVATE,
    RE_DBG_MAP_STACK,
    RE_DBG_MAP_HEAP
} re_dbg_map_type_t;

typedef struct {
    uint64_t base;
    uint64_t size;
    uint32_t protect; /* RE_DBG_PROT_* */
    re_dbg_map_type_t type;
    char path[260];
} re_dbg_map_t;

#define RE_DBG_BP_COND_MAX 96
#define RE_DBG_MAX_VARS 32
#define RE_DBG_MAX_WATCH 32
#define RE_DBG_BP_LOG_MAX 16
#define RE_DBG_NOTE_MAX 32
#define RE_DBG_NOTE_LEN 96
#define RE_DBG_TRACE_MAX 256

/* Trace record v0 (Pack I paging surface; Pack G fills richer streams). */
typedef struct {
    uint64_t rip;
    uint64_t rax;
    uint64_t rcx;
    uint64_t rdx;
} re_dbg_trace_rec_t;

typedef struct {
    uint64_t addr;
    uint8_t orig;
    bool enabled;
    bool temporary;
    bool fast_resume;
    uint32_t hit_count;
    char break_cond[RE_DBG_BP_COND_MAX];
    char log_text[RE_DBG_BP_COND_MAX];
    char log_cond[RE_DBG_BP_COND_MAX];
    char cmd[RE_DBG_BP_COND_MAX];
    char cmd_cond[RE_DBG_BP_COND_MAX];
} re_dbg_bp_info_t;

typedef struct {
    char name[32];
    int64_t value;
    bool used;
} re_dbg_var_t;

typedef struct {
    char expr[RE_DBG_BP_COND_MAX];
    bool used;
} re_dbg_watch_t;

typedef struct {
    uint64_t addr;
    char name[128];
    char module[64];
} re_dbg_symbol_t;

typedef struct {
    uint64_t ip;
    uint64_t sp;
    uint64_t fp;
    char module[64];
    char symbol[128];
} re_dbg_frame_t;

typedef struct re_dbg_bps re_dbg_bps_t;
typedef struct re_dbg_advbp re_dbg_advbp_t;
typedef struct re_dbg_trace re_dbg_trace_t;
typedef struct re_dbg_backend re_dbg_backend_t;

// Stable OS backend contract. Parallel Linux/Windows PRs implement these ops;
// stubs return RE_E_UNSUPPORTED so CLI/MCP can degrade gracefully.
typedef struct re_dbg_backend_vtbl {
    re_err_code_t (*launch)(re_dbg_backend_t *b, const char *path, const char *args);
    re_err_code_t (*attach)(re_dbg_backend_t *b, uint32_t pid);
    re_err_code_t (*detach)(re_dbg_backend_t *b);
    re_err_code_t (*kill)(re_dbg_backend_t *b);
    re_err_code_t (*wait_event)(re_dbg_backend_t *b, re_dbg_event_t *ev, uint32_t timeout_ms);
    re_err_code_t (*cont)(re_dbg_backend_t *b);
    re_err_code_t (*step)(re_dbg_backend_t *b);
    re_err_code_t (*pause)(re_dbg_backend_t *b);
    re_err_code_t (*get_regs)(re_dbg_backend_t *b, uint32_t tid, re_dbg_regs_t *out);
    re_err_code_t (*set_regs)(re_dbg_backend_t *b, uint32_t tid, const re_dbg_regs_t *in);
    re_err_code_t (*read_mem)(re_dbg_backend_t *b, uint64_t addr, void *dst, size_t n);
    re_err_code_t (*write_mem)(re_dbg_backend_t *b, uint64_t addr, const void *src, size_t n);
    re_err_code_t (*enum_threads)(re_dbg_backend_t *b, re_dbg_thread_t *out, size_t cap,
                                  size_t *n_out);
    re_err_code_t (*enum_modules)(re_dbg_backend_t *b, re_dbg_module_t *out, size_t cap,
                                  size_t *n_out);
    re_err_code_t (*enum_maps)(re_dbg_backend_t *b, re_dbg_map_t *out, size_t cap, size_t *n_out);
    re_err_code_t (*thread_suspend)(re_dbg_backend_t *b, uint32_t tid);
    re_err_code_t (*thread_resume)(re_dbg_backend_t *b, uint32_t tid);
    /* size: 1/2/4/8; access: 0=exec, 1=write, 3=read/write (DR7 encoding). */
    re_err_code_t (*hw_bp_set)(re_dbg_backend_t *b, int slot, uint64_t addr, uint8_t size,
                               uint8_t access);
    re_err_code_t (*hw_bp_clear)(re_dbg_backend_t *b, int slot);
    re_err_code_t (*hw_bp_hit)(re_dbg_backend_t *b, uint32_t tid, int *slot_out,
                               uint64_t *addr_out);
    /* Linux: ld.so r_brk; Windows: 0 (LOAD_DLL events cover load BP). */
    re_err_code_t (*rendezvous_addr)(re_dbg_backend_t *b, uint64_t *addr_out);
    /* Before cont after EXCEPTION: pass=true delivers code (sig/EXCEPTION_*), else swallow. */
    re_err_code_t (*set_pass)(re_dbg_backend_t *b, bool pass, int32_t code);
    void (*destroy)(re_dbg_backend_t *b);
} re_dbg_backend_vtbl_t;

struct re_dbg_backend {
    const re_dbg_backend_vtbl_t *vt;
    void *impl;
    re_dbg_caps_t caps;
};

// Long-lived debug session. Lives longer than a single CLI/MCP command arena.
// Session arena owns the backend object; command arenas must never free it.
// Soft-BP table is owned here so wait/cont can restore and re-arm without OS code.
typedef struct re_dbg_session {
    re_arena_t arena;
    re_dbg_backend_t *backend;
    bool attached;
    uint32_t pid;
    uint32_t tid;
    char path[512];
    re_dbg_event_t last_event;
    re_err_t last_err;
    re_disasm_cache_t disasm_cache;
    re_dbg_bps_t *bps;
    re_dbg_advbp_t *advbp;
    re_dbg_trace_t *trace;   /* insn stream + regs; Pack G */
    re_dbg_db_t db;          /* per-module annotations; load on launch, save on exit */
    re_patch_list_t patches; /* live assemble/mem patches for list + write-back */
    re_dbg_var_t vars[RE_DBG_MAX_VARS];
    re_dbg_watch_t watches[RE_DBG_MAX_WATCH];
    char bp_log[RE_DBG_BP_LOG_MAX][RE_DBG_BP_COND_MAX];
    size_t bp_log_n;
    char pending_cmd[RE_DBG_BP_COND_MAX];
    char notes[RE_DBG_NOTE_MAX][RE_DBG_NOTE_LEN];
    size_t notes_n;
    re_dbg_trace_rec_t traces[RE_DBG_TRACE_MAX];
    size_t trace_n;
    uint32_t eval_hit_count; /* $hitcount during conditional BP eval */
    uint64_t follow_dump;    /* maps follow -> dump default address */
} re_dbg_session_t;

re_dbg_backend_t *re_dbg_backend_create(re_arena_t *a);
void re_dbg_session_init(re_dbg_session_t *s);
void re_dbg_session_reset(re_dbg_session_t *s);
void re_dbg_session_free(re_dbg_session_t *s);

// Process-wide session for CLI/MCP until Phase 1 multi-session lands.
re_dbg_session_t *re_dbg_session_global(void);

// Session control: CLI/MCP/TUI call these; they dispatch to the backend vtable
// and update pid/tid/last_event on success. Stubs return RE_E_UNSUPPORTED.
re_err_code_t re_dbg_session_launch(re_dbg_session_t *s, const char *path, const char *args);
re_err_code_t re_dbg_session_attach(re_dbg_session_t *s, uint32_t pid);
re_err_code_t re_dbg_session_detach(re_dbg_session_t *s);
re_err_code_t re_dbg_session_kill(re_dbg_session_t *s);
re_err_code_t re_dbg_session_cont(re_dbg_session_t *s);
re_err_code_t re_dbg_session_step(re_dbg_session_t *s);
re_err_code_t re_dbg_session_step_over(re_dbg_session_t *s);
re_err_code_t re_dbg_session_pause(re_dbg_session_t *s);
re_err_code_t re_dbg_session_wait_event(re_dbg_session_t *s, re_dbg_event_t *ev,
                                        uint32_t timeout_ms);
re_err_code_t re_dbg_session_get_regs(re_dbg_session_t *s, uint32_t tid, re_dbg_regs_t *out);
re_err_code_t re_dbg_session_set_regs(re_dbg_session_t *s, uint32_t tid, const re_dbg_regs_t *in);
re_err_code_t re_dbg_session_read_mem(re_dbg_session_t *s, uint64_t addr, void *dst, size_t n);
re_err_code_t re_dbg_session_write_mem(re_dbg_session_t *s, uint64_t addr, const void *src,
                                       size_t n);
re_err_code_t re_dbg_session_disasm(re_dbg_session_t *s, uint64_t addr, size_t count,
                                    re_disasm_insn_t *out, size_t *n_out);
re_err_code_t re_dbg_session_bp_set(re_dbg_session_t *s, uint64_t addr);
re_err_code_t re_dbg_session_bp_clear(re_dbg_session_t *s, uint64_t addr);
re_err_code_t re_dbg_session_bp_enable(re_dbg_session_t *s, uint64_t addr);
re_err_code_t re_dbg_session_bp_disable(re_dbg_session_t *s, uint64_t addr);
re_err_code_t re_dbg_session_bp_list(re_dbg_session_t *s, re_dbg_bp_info_t *out, size_t cap,
                                     size_t *n_out);
re_err_code_t re_dbg_session_bp_set_cond(re_dbg_session_t *s, uint64_t addr, const char *field,
                                         const char *value);

/* Exception/signal BP + ignore/pass ranges (first-chance aware). */
re_err_code_t re_dbg_session_ex_set(re_dbg_session_t *s, int32_t lo, int32_t hi,
                                    re_dbg_ex_policy_t policy, bool first_chance_only);
re_err_code_t re_dbg_session_ex_clear(re_dbg_session_t *s, int32_t lo, int32_t hi);
re_err_code_t re_dbg_session_ex_list(re_dbg_session_t *s, re_dbg_ex_rule_t *out, size_t cap,
                                     size_t *n_out);
re_dbg_caps_t re_dbg_session_caps(const re_dbg_session_t *s);

const char *re_dbg_event_kind_str(re_dbg_event_kind_t kind);
const char *re_dbg_ex_policy_str(re_dbg_ex_policy_t p);
const char *re_dbg_backend_name(void);
re_dbg_caps_t re_dbg_backend_caps(const re_dbg_backend_t *b);

// Module list, memory maps, threads, symbol resolve, and stack walk.
re_err_code_t re_dbg_session_enum_modules(re_dbg_session_t *s, re_dbg_module_t *out, size_t cap,
                                          size_t *n_out);
re_err_code_t re_dbg_session_enum_maps(re_dbg_session_t *s, re_dbg_map_t *out, size_t cap,
                                       size_t *n_out);
re_err_code_t re_dbg_session_follow_dump(re_dbg_session_t *s, uint64_t addr);
re_err_code_t re_dbg_session_enum_threads(re_dbg_session_t *s, re_dbg_thread_t *out, size_t cap,
                                          size_t *n_out);
re_err_code_t re_dbg_session_thread_switch(re_dbg_session_t *s, uint32_t tid);
re_err_code_t re_dbg_session_thread_suspend(re_dbg_session_t *s, uint32_t tid);
re_err_code_t re_dbg_session_thread_resume(re_dbg_session_t *s, uint32_t tid);
re_err_code_t re_dbg_sym_resolve_file(re_arena_t *a, const char *path, uint64_t base,
                                      const char *name, uint64_t *addr_out);
re_err_code_t re_dbg_sym_nearest_file(re_arena_t *a, const char *path, uint64_t base, uint64_t addr,
                                      char *name_out, size_t name_cap, uint64_t *sym_addr_out);
re_err_code_t re_dbg_session_resolve(re_dbg_session_t *s, const char *query, uint64_t *addr_out,
                                     re_dbg_symbol_t *info_out);
re_err_code_t re_dbg_session_bt(re_dbg_session_t *s, re_dbg_frame_t *out, size_t cap,
                                size_t *n_out);
const char *re_dbg_map_type_str(re_dbg_map_type_t t);
void re_dbg_map_prot_str(uint32_t protect, char out[4]);

// Expression / $vars / watch window (CLI/MCP; TUI hook via re_cpu_fill_watches).
re_err_code_t re_dbg_session_eval(re_dbg_session_t *s, const char *expr, int64_t *out);
re_err_code_t re_dbg_session_var_get(re_dbg_session_t *s, const char *name, int64_t *out);
re_err_code_t re_dbg_session_var_set(re_dbg_session_t *s, const char *name, int64_t value);
re_err_code_t re_dbg_session_watch_add(re_dbg_session_t *s, const char *expr);
re_err_code_t re_dbg_session_watch_clear(re_dbg_session_t *s, size_t index_or_all);
re_err_code_t re_dbg_session_watch_list(re_dbg_session_t *s, re_dbg_watch_t *out, size_t cap,
                                        size_t *n_out);
re_err_code_t re_dbg_session_watch_eval(re_dbg_session_t *s, size_t index, int64_t *value_out,
                                        char *err, size_t err_cap);
// After a software BP hit: hit count, log/cmd conditions, fast resume.
// Returns false when the break should be swallowed (auto-continue scheduled).
bool re_dbg_session_cond_on_break(re_dbg_session_t *s, re_dbg_event_t *ev);

#ifdef __cplusplus
}
#endif
