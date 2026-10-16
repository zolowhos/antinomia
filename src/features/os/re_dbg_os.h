// re_dbg_os.h - OS extras: SEH/handles (Win), signals/build-id (Linux).
// Module: feature/os (C11).
// Owns: session APIs for Phase 2 Wave 4 OS views and module build-id enrich.
// Depends: re_dbg session. OS headers stay inside the os_*.c units.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#include "features/debug/re_dbg.h"
#include "utils/sys/re_err.h"

#define RE_DBG_SEH_MAX 64
#define RE_DBG_HANDLE_MAX 256
#define RE_DBG_SIG_MAX 64
#define RE_DBG_BUILD_ID_MAX 48

typedef struct {
    uint32_t tid;
    uint64_t frame;   /* EXCEPTION_REGISTRATION_RECORD address */
    uint64_t handler; /* handler pointer */
    uint64_t next;    /* next record (or ~0) */
} re_dbg_seh_frame_t;

typedef struct {
    uint64_t handle; /* handle value in the target */
    uint32_t type_index;
    uint32_t granted_access;
    char type_name[32];
    char object_name[96];
} re_dbg_handle_t;

typedef struct {
    int32_t signo;
    char name[16];
    char disposition[16]; /* default | blocked | ignored | caught */
} re_dbg_sig_disp_t;

/* Fill build_id hex on modules that have a readable ELF/PE image path. */
re_err_code_t re_dbg_os_enrich_modules(re_dbg_session_t *s, re_dbg_module_t *mods, size_t n);

/* Win: walk TEB ExceptionList per thread (x86 SEH; often empty on native x64). */
re_err_code_t re_dbg_session_enum_seh(re_dbg_session_t *s, re_dbg_seh_frame_t *out, size_t cap,
                                      size_t *n_out);

/* Win: enumerate open handles for the debuggee pid (best-effort names). */
re_err_code_t re_dbg_session_enum_handles(re_dbg_session_t *s, re_dbg_handle_t *out, size_t cap,
                                          size_t *n_out);

/* Linux: signal disposition from /proc/pid/status SigBlk/SigIgn/SigCgt. */
re_err_code_t re_dbg_session_enum_signals(re_dbg_session_t *s, re_dbg_sig_disp_t *out, size_t cap,
                                          size_t *n_out);

/* Force-write the per-module annotation DB beside the image. */
re_err_code_t re_dbg_session_db_save(re_dbg_session_t *s);

#ifdef __cplusplus
}
#endif
