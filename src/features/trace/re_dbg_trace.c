// re_dbg_trace.c - ring-buffer capture of instruction stream and GPRs.
// Module: feature (C11).
// Owns: start/stop/clear/step/query and capture hooks for the session.
// Depends: re_dbg_trace.h, re_disasm. Session arena owns the buffer object.
#include "features/trace/re_dbg_trace.h"

#include "features/disasm/re_disasm.h"

#include <string.h>

static re_err_code_t note(re_dbg_session_t *s, re_err_code_t c, const char *msg) {
    if (!s)
        return c == RE_OK ? RE_E_INTERNAL : c;
    if (c == RE_OK)
        RE_ERR_OK(&s->last_err);
    else
        RE_ERR_SET(&s->last_err, c, msg);
    return c;
}

void re_dbg_trace_clear(re_dbg_trace_t *t) {
    if (!t)
        return;
    t->recording = false;
    t->count = 0;
    t->head = 0;
    t->next_index = 0;
    t->dropped = 0;
    memset(t->frames, 0, sizeof(t->frames));
}

static re_dbg_trace_t *need_trace(re_dbg_session_t *s) {
    if (!s)
        return NULL;
    if (!s->trace) {
        s->trace = (re_dbg_trace_t *)re_arena_calloc(&s->arena, 1, sizeof(*s->trace));
        if (s->trace)
            re_dbg_trace_clear(s->trace);
    }
    return s->trace;
}

static void copy_text(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    if (!dst || cap == 0)
        return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void push_frame(re_dbg_trace_t *t, const re_dbg_trace_frame_t *f) {
    t->frames[t->head] = *f;
    t->head = (t->head + 1u) % RE_TRACE_CAP;
    if (t->count < RE_TRACE_CAP)
        t->count++;
    else
        t->dropped++;
    t->next_index++;
}

static size_t ring_index(const re_dbg_trace_t *t, size_t logical) {
    size_t start;
    if (logical >= t->count)
        return 0;
    if (t->count < RE_TRACE_CAP)
        start = 0;
    else
        start = t->head;
    return (start + logical) % RE_TRACE_CAP;
}

re_err_code_t re_dbg_session_trace_start(re_dbg_session_t *s) {
    re_dbg_trace_t *t = need_trace(s);
    if (!t)
        return note(s, RE_E_INTERNAL, "trace buffer alloc failed");
    t->recording = true;
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_trace_stop(re_dbg_session_t *s) {
    re_dbg_trace_t *t = need_trace(s);
    if (!t)
        return note(s, RE_E_INTERNAL, "trace buffer alloc failed");
    t->recording = false;
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_trace_clear(re_dbg_session_t *s) {
    re_dbg_trace_t *t = need_trace(s);
    if (!t)
        return note(s, RE_E_INTERNAL, "trace buffer alloc failed");
    re_dbg_trace_clear(t);
    return note(s, RE_OK, NULL);
}

bool re_dbg_session_trace_recording(const re_dbg_session_t *s) {
    return s && s->trace && s->trace->recording;
}

size_t re_dbg_session_trace_count(const re_dbg_session_t *s) {
    return s && s->trace ? s->trace->count : 0;
}

re_err_code_t re_dbg_session_trace_capture(re_dbg_session_t *s) {
    re_dbg_trace_t *t;
    re_dbg_regs_t regs;
    re_disasm_insn_t insn;
    size_t n = 0;
    re_dbg_trace_frame_t f;
    re_err_code_t c;
    if (!s || !s->attached)
        return RE_E_USAGE;
    t = need_trace(s);
    if (!t)
        return note(s, RE_E_INTERNAL, "trace buffer alloc failed");
    if (!t->recording)
        return RE_OK;
    c = re_dbg_session_get_regs(s, s->tid, &regs);
    if (c != RE_OK)
        return c;
    memset(&insn, 0, sizeof(insn));
    c = re_dbg_session_disasm(s, regs.rip, 1, &insn, &n);
    if (c != RE_OK || n == 0) {
        /* Still record regs even if decode fails. */
        insn.addr = regs.rip;
        insn.size = 0;
        copy_text(insn.text, sizeof(insn.text), "???");
    }
    memset(&f, 0, sizeof(f));
    f.index = t->next_index;
    f.tid = s->tid;
    f.size = insn.size;
    if (f.size > RE_TRACE_BYTES_MAX)
        f.size = RE_TRACE_BYTES_MAX;
    if (f.size)
        memcpy(f.bytes, insn.bytes, f.size);
    copy_text(f.text, sizeof(f.text), insn.text[0] ? insn.text : "???");
    f.regs = regs;
    push_frame(t, &f);
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_trace_step(re_dbg_session_t *s, uint32_t n) {
    re_dbg_trace_t *t;
    bool was;
    uint32_t i;
    re_err_code_t c;
    if (!s || !s->attached)
        return note(s, RE_E_USAGE, "trace step needs an attached target");
    if (n == 0)
        n = 1;
    t = need_trace(s);
    if (!t)
        return note(s, RE_E_INTERNAL, "trace buffer alloc failed");
    was = t->recording;
    t->recording = true;
    /* Record the instruction at the current stop, then each landing after step. */
    c = re_dbg_session_trace_capture(s);
    if (c != RE_OK) {
        t->recording = was;
        return c;
    }
    for (i = 0; i < n; i++) {
        re_dbg_event_t ev;
        c = re_dbg_session_step(s);
        if (c != RE_OK) {
            t->recording = was;
            return c;
        }
        c = re_dbg_session_wait_event(s, &ev, 2000);
        if (c != RE_OK) {
            t->recording = was;
            return c;
        }
        if (ev.kind == RE_DBG_EV_EXIT) {
            t->recording = was;
            return note(s, RE_OK, NULL);
        }
    }
    t->recording = was;
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_trace_query(re_dbg_session_t *s, size_t offset, size_t limit,
                                         re_dbg_trace_frame_t *out, size_t cap, size_t *n_out,
                                         size_t *total_out, bool *truncated) {
    re_dbg_trace_t *t;
    size_t total;
    size_t i;
    size_t n;
    if (n_out)
        *n_out = 0;
    if (total_out)
        *total_out = 0;
    if (truncated)
        *truncated = false;
    t = need_trace(s);
    if (!t)
        return note(s, RE_E_INTERNAL, "trace buffer alloc failed");
    if (!out || cap == 0)
        return note(s, RE_E_USAGE, "trace query needs an output buffer");
    total = t->count;
    if (total_out)
        *total_out = total;
    if (offset > total)
        offset = total;
    n = total - offset;
    if (limit == 0)
        limit = 200;
    if (n > limit)
        n = limit;
    if (n > cap)
        n = cap;
    if (offset + n < total && truncated)
        *truncated = true;
    for (i = 0; i < n; i++)
        out[i] = t->frames[ring_index(t, offset + i)];
    if (n_out)
        *n_out = n;
    return note(s, RE_OK, NULL);
}
