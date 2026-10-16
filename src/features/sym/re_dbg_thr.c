// re_dbg_thr.c - session memory-map and thread control helpers.
// Module: feature/sym (C11).
// Owns: enum_maps/follow_dump and thread switch/suspend/resume session APIs.
// Depends: re_dbg.h. Dispatches to backend vtable; no OS headers here.
#include "features/debug/re_dbg.h"

#include <string.h>

const char *re_dbg_map_type_str(re_dbg_map_type_t t) {
    switch (t) {
        case RE_DBG_MAP_IMAGE:
            return "image";
        case RE_DBG_MAP_MAPPED:
            return "mapped";
        case RE_DBG_MAP_PRIVATE:
            return "private";
        case RE_DBG_MAP_STACK:
            return "stack";
        case RE_DBG_MAP_HEAP:
            return "heap";
        default:
            return "other";
    }
}

void re_dbg_map_prot_str(uint32_t protect, char out[4]) {
    if (!out)
        return;
    out[0] = (protect & RE_DBG_PROT_R) ? 'r' : '-';
    out[1] = (protect & RE_DBG_PROT_W) ? 'w' : '-';
    out[2] = (protect & RE_DBG_PROT_X) ? 'x' : '-';
    out[3] = 0;
}

static re_err_code_t note(re_dbg_session_t *s, re_err_code_t c, const char *msg) {
    if (!s)
        return c == RE_OK ? RE_E_INTERNAL : c;
    if (c == RE_OK)
        RE_ERR_OK(&s->last_err);
    else
        RE_ERR_SET(&s->last_err, c, msg);
    return c;
}

re_err_code_t re_dbg_session_enum_maps(re_dbg_session_t *s, re_dbg_map_t *out, size_t cap,
                                       size_t *n_out) {
    if (n_out)
        *n_out = 0;
    if (!s || !s->backend || !s->backend->vt || !s->backend->vt->enum_maps)
        return RE_E_INTERNAL;
    if (!s->attached)
        return note(s, RE_E_USAGE, "maps requires an attached target");
    return note(s, s->backend->vt->enum_maps(s->backend, out, cap, n_out), "enum_maps failed");
}

re_err_code_t re_dbg_session_follow_dump(re_dbg_session_t *s, uint64_t addr) {
    if (!s)
        return RE_E_INTERNAL;
    if (!s->attached)
        return note(s, RE_E_USAGE, "follow requires an attached target");
    s->follow_dump = addr;
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_enum_threads(re_dbg_session_t *s, re_dbg_thread_t *out, size_t cap,
                                          size_t *n_out) {
    if (n_out)
        *n_out = 0;
    if (!s || !s->backend || !s->backend->vt || !s->backend->vt->enum_threads)
        return RE_E_INTERNAL;
    if (!s->attached)
        return note(s, RE_E_USAGE, "threads requires an attached target");
    return note(s, s->backend->vt->enum_threads(s->backend, out, cap, n_out),
                "enum_threads failed");
}

re_err_code_t re_dbg_session_thread_switch(re_dbg_session_t *s, uint32_t tid) {
    re_dbg_thread_t list[64];
    size_t n = 0, i;
    re_err_code_t c;
    if (!s || !s->backend || !s->backend->vt)
        return RE_E_INTERNAL;
    if (!s->attached)
        return note(s, RE_E_USAGE, "thread switch requires an attached target");
    if (tid == 0)
        return note(s, RE_E_USAGE, "thread switch needs a tid");
    c = s->backend->vt->enum_threads(s->backend, list, 64, &n);
    if (c != RE_OK)
        return note(s, c, "enum_threads failed");
    for (i = 0; i < n; i++) {
        re_dbg_regs_t regs;
        if (list[i].tid != tid)
            continue;
        s->tid = tid;
        /* Focus backend current tid via get_regs side effect (cur_tid/last_tid). */
        memset(&regs, 0, sizeof(regs));
        (void)s->backend->vt->get_regs(s->backend, tid, &regs);
        return note(s, RE_OK, NULL);
    }
    return note(s, RE_E_NOTFOUND, "thread not found");
}

re_err_code_t re_dbg_session_thread_suspend(re_dbg_session_t *s, uint32_t tid) {
    re_err_code_t c;
    if (!s || !s->backend || !s->backend->vt || !s->backend->vt->thread_suspend)
        return RE_E_INTERNAL;
    if (!s->attached)
        return note(s, RE_E_USAGE, "suspend requires an attached target");
    c = s->backend->vt->thread_suspend(s->backend, tid ? tid : s->tid);
    return note(s, c, c == RE_OK ? NULL : "thread suspend failed");
}

re_err_code_t re_dbg_session_thread_resume(re_dbg_session_t *s, uint32_t tid) {
    re_err_code_t c;
    if (!s || !s->backend || !s->backend->vt || !s->backend->vt->thread_resume)
        return RE_E_INTERNAL;
    if (!s->attached)
        return note(s, RE_E_USAGE, "resume requires an attached target");
    c = s->backend->vt->thread_resume(s->backend, tid ? tid : s->tid);
    return note(s, c, c == RE_OK ? NULL : "thread resume failed");
}
