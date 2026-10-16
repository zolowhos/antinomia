// re_dbg_backend.c - pick the host OS stub backend that still links.
// Module: feature (C11).
// Owns: backend factory. Real ptrace / WinDebug arrives in Phase 1.
// Depends: re_dbg.h. Both stubs compile; only one is selected at runtime.
#include "features/debug/re_dbg.h"

#include <string.h>

re_dbg_backend_t *re_dbg_backend_linux_create(re_arena_t *a);
re_dbg_backend_t *re_dbg_backend_win_create(re_arena_t *a);

re_dbg_backend_t *re_dbg_backend_create(re_arena_t *a) {
#if defined(_WIN32)
    return re_dbg_backend_win_create(a);
#else
    return re_dbg_backend_linux_create(a);
#endif
}
