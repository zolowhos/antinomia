// re_dbg_session.c - long-lived debug session arena and global handle.
// Module: feature (C11).
// Owns: session init/reset/free and the process-wide session pointer.
// Depends: re_dbg.h. Session arena is not the per-command arena.
#include "features/debug/re_dbg.h"

#include <string.h>

static re_dbg_session_t g_session;
static bool g_ready;

void re_dbg_session_init(re_dbg_session_t *s) {
    memset(s, 0, sizeof(*s));
    re_arena_init(&s->arena, 64u * 1024u);
    s->backend = re_dbg_backend_create(&s->arena);
    s->last_err.code = RE_OK;
}

void re_dbg_session_reset(re_dbg_session_t *s) {
    if (s->backend && s->backend->vt && s->backend->vt->detach && s->attached)
        s->backend->vt->detach(s->backend);
    s->attached = false;
    s->pid = 0;
    s->path[0] = 0;
    RE_ERR_OK(&s->last_err);
    re_arena_reset(&s->arena);
    s->backend = re_dbg_backend_create(&s->arena);
}

void re_dbg_session_free(re_dbg_session_t *s) {
    if (s->backend && s->backend->vt && s->backend->vt->destroy)
        s->backend->vt->destroy(s->backend);
    s->backend = NULL;
    re_arena_free(&s->arena);
}

re_dbg_session_t *re_dbg_session_global(void) {
    if (!g_ready) {
        re_dbg_session_init(&g_session);
        g_ready = true;
    }
    return &g_session;
}
