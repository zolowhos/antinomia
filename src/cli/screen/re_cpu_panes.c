// re_cpu_panes.c - maps, threads, and call-stack fill helpers for the CPU view.
// Module: cli (C11).
// Owns: re_cpu_fill_maps/threads/callstack used by re_cpu_refresh.
// Depends: re_cpu.h, re_dbg. Keeps re_cpu.c under the line cap.
#include "cli/screen/re_cpu.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void line_set(re_cpu_line_t *L, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(L->text, sizeof(L->text), fmt, ap);
    va_end(ap);
}

void re_cpu_fill_maps(re_cpu_view_t *v, re_dbg_session_t *s) {
    re_dbg_map_t maps[48];
    size_t n = 0, i;
    v->n_maps = 0;
    if (!s || !s->attached) {
        line_set(&v->map_lines[v->n_maps++], "(no maps)");
        return;
    }
    if (re_dbg_session_enum_maps(s, maps, 48, &n) != RE_OK || n == 0) {
        line_set(&v->map_lines[v->n_maps++], "(maps unavailable)");
        return;
    }
    for (i = 0; i < n && v->n_maps < RE_CPU_MAP_MAX; i++) {
        char prot[4];
        re_dbg_map_prot_str(maps[i].protect, prot);
        line_set(&v->map_lines[v->n_maps++], "%016llx %s %-7s %s", (unsigned long long)maps[i].base,
                 prot, re_dbg_map_type_str(maps[i].type), maps[i].path[0] ? maps[i].path : "-");
    }
}

void re_cpu_fill_threads(re_cpu_view_t *v, re_dbg_session_t *s) {
    re_dbg_thread_t thr[32];
    size_t n = 0, i;
    v->n_threads = 0;
    if (!s || !s->attached) {
        line_set(&v->thr_lines[v->n_threads++], "(no threads)");
        return;
    }
    if (re_dbg_session_enum_threads(s, thr, 32, &n) != RE_OK || n == 0) {
        line_set(&v->thr_lines[v->n_threads++], "(threads unavailable)");
        return;
    }
    for (i = 0; i < n && v->n_threads < RE_CPU_THR_MAX; i++) {
        line_set(&v->thr_lines[v->n_threads++], "%c tid=%u rip=%016llx%s",
                 thr[i].tid == s->tid ? '*' : ' ', (unsigned)thr[i].tid,
                 (unsigned long long)thr[i].rip, thr[i].suspended ? " susp" : "");
    }
}

void re_cpu_fill_callstack(re_cpu_view_t *v, re_dbg_session_t *s) {
    re_dbg_frame_t frames[RE_CPU_STACK_MAX];
    size_t n = 0, i;
    v->n_stack = 0;
    if (!s || !s->attached) {
        line_set(&v->stack[v->n_stack++], "(no stack)");
        return;
    }
    if (re_dbg_session_bt(s, frames, RE_CPU_STACK_MAX, &n) != RE_OK || n == 0) {
        line_set(&v->stack[v->n_stack++], "(call stack unavailable)");
        return;
    }
    for (i = 0; i < n && v->n_stack < RE_CPU_STACK_MAX; i++) {
        if (frames[i].module[0] && frames[i].symbol[0])
            line_set(&v->stack[v->n_stack++], "#%zu %s!%s", i, frames[i].module, frames[i].symbol);
        else if (frames[i].module[0])
            line_set(&v->stack[v->n_stack++], "#%zu %s+%016llx", i, frames[i].module,
                     (unsigned long long)frames[i].ip);
        else
            line_set(&v->stack[v->n_stack++], "#%zu %016llx", i, (unsigned long long)frames[i].ip);
    }
}
