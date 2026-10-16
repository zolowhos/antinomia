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

#include "utils/mem/re_arena.h"
#include "utils/sys/re_err.h"

typedef enum {
    RE_DBG_EV_NONE = 0,
    RE_DBG_EV_CREATE,
    RE_DBG_EV_EXIT,
    RE_DBG_EV_BREAK,
    RE_DBG_EV_STEP,
    RE_DBG_EV_EXCEPTION,
    RE_DBG_EV_LOAD_MODULE,
    RE_DBG_EV_UNLOAD_MODULE,
    RE_DBG_EV_THREAD
} re_dbg_event_kind_t;

typedef struct {
    re_dbg_event_kind_t kind;
    uint32_t pid;
    uint32_t tid;
    uint64_t addr;
    int32_t code;
} re_dbg_event_t;

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
} re_dbg_thread_t;

typedef struct {
    uint64_t base;
    uint64_t size;
    char path[260];
} re_dbg_module_t;

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
    re_err_code_t (*get_regs)(re_dbg_backend_t *b, uint32_t tid, re_dbg_regs_t *out);
    re_err_code_t (*set_regs)(re_dbg_backend_t *b, uint32_t tid, const re_dbg_regs_t *in);
    re_err_code_t (*read_mem)(re_dbg_backend_t *b, uint64_t addr, void *dst, size_t n);
    re_err_code_t (*write_mem)(re_dbg_backend_t *b, uint64_t addr, const void *src, size_t n);
    re_err_code_t (*enum_threads)(re_dbg_backend_t *b, re_dbg_thread_t *out, size_t cap,
                                  size_t *n_out);
    re_err_code_t (*enum_modules)(re_dbg_backend_t *b, re_dbg_module_t *out, size_t cap,
                                  size_t *n_out);
    void (*destroy)(re_dbg_backend_t *b);
} re_dbg_backend_vtbl_t;

struct re_dbg_backend {
    const re_dbg_backend_vtbl_t *vt;
    void *impl;
};

// Long-lived debug session. Lives longer than a single CLI/MCP command arena.
// Session arena owns the backend object; command arenas must never free it.
typedef struct re_dbg_session {
    re_arena_t arena;
    re_dbg_backend_t *backend;
    bool attached;
    uint32_t pid;
    uint32_t tid;
    char path[512];
    re_dbg_event_t last_event;
    re_err_t last_err;
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
re_err_code_t re_dbg_session_wait_event(re_dbg_session_t *s, re_dbg_event_t *ev,
                                        uint32_t timeout_ms);

const char *re_dbg_event_kind_str(re_dbg_event_kind_t kind);
const char *re_dbg_backend_name(void);

// Module list, symbol resolve v0 (PE export / ELF dynsym), and stack walk v0.
re_err_code_t re_dbg_session_enum_modules(re_dbg_session_t *s, re_dbg_module_t *out, size_t cap,
                                          size_t *n_out);
re_err_code_t re_dbg_sym_resolve_file(re_arena_t *a, const char *path, uint64_t base,
                                      const char *name, uint64_t *addr_out);
re_err_code_t re_dbg_session_resolve(re_dbg_session_t *s, const char *query, uint64_t *addr_out,
                                     re_dbg_symbol_t *info_out);
re_err_code_t re_dbg_session_bt(re_dbg_session_t *s, re_dbg_frame_t *out, size_t cap,
                                size_t *n_out);

#ifdef __cplusplus
}
#endif
