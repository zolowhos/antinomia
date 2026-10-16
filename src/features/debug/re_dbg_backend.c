// re_dbg_backend.c - pick the host OS debug backend that still links.
// Module: feature (C11).
// Owns: backend factory. Linux ptrace is live; WinDebug arrives in Pack B.
// Depends: re_dbg.h. Both backends compile; only one is selected at runtime.
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
