// re_dbg_trace_io.c - save and load antinomia/1 instruction-trace JSON files.
// Module: feature (C11).
// Owns: on-disk schema for Pack G traces (frames with regs + insn bytes).
// Depends: re_dbg_trace.h, re_json, re_jr, re_buf. Arena scratch only.
#include "features/trace/re_dbg_trace.h"

#include "utils/json/re_jr.h"
#include "utils/json/re_json.h"
#include "utils/mem/re_buf.h"
#include "utils/text/re_hex.h"
#include "utils/text/re_str.h"

#include <stdio.h>
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

static void emit_regs(re_jw_t *w, const re_dbg_regs_t *r) {
    re_jw_key(w, "regs");
    re_jw_obj(w);
    re_jw_khex(w, "rax", r->rax, 16);
    re_jw_khex(w, "rbx", r->rbx, 16);
    re_jw_khex(w, "rcx", r->rcx, 16);
    re_jw_khex(w, "rdx", r->rdx, 16);
    re_jw_khex(w, "rsi", r->rsi, 16);
    re_jw_khex(w, "rdi", r->rdi, 16);
    re_jw_khex(w, "rbp", r->rbp, 16);
    re_jw_khex(w, "rsp", r->rsp, 16);
    re_jw_khex(w, "r8", r->r8, 16);
    re_jw_khex(w, "r9", r->r9, 16);
    re_jw_khex(w, "r10", r->r10, 16);
    re_jw_khex(w, "r11", r->r11, 16);
    re_jw_khex(w, "r12", r->r12, 16);
    re_jw_khex(w, "r13", r->r13, 16);
    re_jw_khex(w, "r14", r->r14, 16);
    re_jw_khex(w, "r15", r->r15, 16);
    re_jw_khex(w, "rip", r->rip, 16);
    re_jw_khex(w, "rflags", r->rflags, 16);
    re_jw_obj_end(w);
}

static uint64_t parse_u64(const re_jr_t *v) {
    re_str_t s;
    uint64_t n = 0;
    size_t i = 0;
    if (!v)
        return 0;
    /* re_jr keeps numbers as raw text; v->num is always 0 (see re_jr.c). */
    if (v->kind == RE_JR_NUM)
        return (uint64_t)re_jr_i64(v, 0);
    if (v->kind != RE_JR_STR)
        return 0;
    s = v->str;
    if (s.n >= 2 && s.p[0] == '0' && (s.p[1] == 'x' || s.p[1] == 'X'))
        i = 2;
    for (; i < s.n; i++) {
        int d = re_hex_val(s.p[i]);
        if (d < 0)
            break;
        n = (n << 4) | (uint64_t)d;
    }
    return n;
}

static void load_regs(re_dbg_regs_t *r, const re_jr_t *o) {
    if (!r || !o || o->kind != RE_JR_OBJ)
        return;
    memset(r, 0, sizeof(*r));
    r->rax = parse_u64(re_jr_get(o, "rax"));
    r->rbx = parse_u64(re_jr_get(o, "rbx"));
    r->rcx = parse_u64(re_jr_get(o, "rcx"));
    r->rdx = parse_u64(re_jr_get(o, "rdx"));
    r->rsi = parse_u64(re_jr_get(o, "rsi"));
    r->rdi = parse_u64(re_jr_get(o, "rdi"));
    r->rbp = parse_u64(re_jr_get(o, "rbp"));
    r->rsp = parse_u64(re_jr_get(o, "rsp"));
    r->r8 = parse_u64(re_jr_get(o, "r8"));
    r->r9 = parse_u64(re_jr_get(o, "r9"));
    r->r10 = parse_u64(re_jr_get(o, "r10"));
    r->r11 = parse_u64(re_jr_get(o, "r11"));
    r->r12 = parse_u64(re_jr_get(o, "r12"));
    r->r13 = parse_u64(re_jr_get(o, "r13"));
    r->r14 = parse_u64(re_jr_get(o, "r14"));
    r->r15 = parse_u64(re_jr_get(o, "r15"));
    r->rip = parse_u64(re_jr_get(o, "rip"));
    r->rflags = parse_u64(re_jr_get(o, "rflags"));
}

static void copy_cstr(char *dst, size_t cap, const char *src, size_t n) {
    size_t i = 0;
    if (!dst || cap == 0)
        return;
    while (src && i < n && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void emit_frames(re_jw_t *w, const re_dbg_trace_t *t) {
    size_t i;
    size_t start = t->count < RE_TRACE_CAP ? 0 : t->head;
    re_jw_key(w, "frames");
    re_jw_arr(w);
    for (i = 0; i < t->count; i++) {
        const re_dbg_trace_frame_t *f = &t->frames[(start + i) % RE_TRACE_CAP];
        char hex[RE_TRACE_BYTES_MAX * 2u + 1u];
        re_jw_obj(w);
        re_jw_ku64(w, "index", f->index);
        re_jw_ku64(w, "tid", f->tid);
        re_jw_khex(w, "rip", f->regs.rip, 16);
        re_jw_ku64(w, "size", f->size);
        if (f->size)
            re_hex_encode(hex, f->bytes, f->size);
        else
            hex[0] = 0;
        re_jw_kcstr(w, "bytes", hex);
        re_jw_kcstr(w, "text", f->text);
        emit_regs(w, &f->regs);
        re_jw_obj_end(w);
    }
    re_jw_arr_end(w);
}

static void load_one_frame(re_dbg_trace_t *t, const re_jr_t *o) {
    const re_jr_t *bytes;
    const re_jr_t *text;
    re_dbg_trace_frame_t fr;
    size_t bn = 0;
    if (!o || o->kind != RE_JR_OBJ || t->count >= RE_TRACE_CAP)
        return;
    memset(&fr, 0, sizeof(fr));
    fr.index = parse_u64(re_jr_get(o, "index"));
    fr.tid = (uint32_t)parse_u64(re_jr_get(o, "tid"));
    fr.size = (uint8_t)parse_u64(re_jr_get(o, "size"));
    bytes = re_jr_get(o, "bytes");
    if (bytes && bytes->kind == RE_JR_STR && bytes->str.n > 0) {
        if (re_hex_decode(fr.bytes, RE_TRACE_BYTES_MAX, bytes->str.p, bytes->str.n, &bn, false))
            fr.size = (uint8_t)bn;
    }
    text = re_jr_get(o, "text");
    if (text && text->kind == RE_JR_STR)
        copy_cstr(fr.text, sizeof(fr.text), text->str.p, text->str.n);
    load_regs(&fr.regs, re_jr_get(o, "regs"));
    if (fr.regs.rip == 0)
        fr.regs.rip = parse_u64(re_jr_get(o, "rip"));
    t->frames[t->count++] = fr;
    if (fr.index + 1u > t->next_index)
        t->next_index = fr.index + 1u;
}

re_err_code_t re_dbg_session_trace_save(re_dbg_session_t *s, const char *path) {
    re_dbg_trace_t *t;
    re_arena_t scratch;
    re_jw_t w;
    FILE *fp;
    if (!s || !path || !path[0])
        return note(s, RE_E_USAGE, "tracesave needs a path");
    t = s->trace;
    if (!t)
        return note(s, RE_E_USAGE, "no trace buffer");
    fp = fopen(path, "wb");
    if (!fp)
        return note(s, RE_E_IO, "tracesave fopen failed");
    re_arena_init(&scratch, 512u * 1024u);
    re_jw_init(&w, &scratch);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", "antinomia/1");
    re_jw_kcstr(&w, "kind", "trace");
    re_jw_ku64(&w, "count", t->count);
    re_jw_ku64(&w, "dropped", t->dropped);
    re_jw_ku64(&w, "next_index", t->next_index);
    emit_frames(&w, t);
    re_jw_obj_end(&w);
    if (!re_jw_ok(&w)) {
        fclose(fp);
        re_arena_free(&scratch);
        return note(s, RE_E_INTERNAL, "tracesave JSON overflow");
    }
    re_jw_flush(&w, fp);
    fclose(fp);
    re_arena_free(&scratch);
    return note(s, RE_OK, NULL);
}

static void copy_replay_path(re_dbg_session_t *s, const char *path) {
    size_t i = 0;
    if (!s)
        return;
    if (!path) {
        s->replay_path[0] = 0;
        return;
    }
    while (path[i] && i + 1u < sizeof(s->replay_path)) {
        s->replay_path[i] = path[i];
        i++;
    }
    s->replay_path[i] = 0;
}

re_err_code_t re_dbg_session_replay_open(re_dbg_session_t *s, const char *path) {
    re_err_code_t c;
    if (!s || !path || !path[0])
        return note(s, RE_E_USAGE, "replay.open needs a Phase 2 trace file path");
    c = re_dbg_session_trace_load(s, path);
    if (c != RE_OK)
        return c;
    s->offline_replay = true;
    copy_replay_path(s, path);
    return note(s, RE_OK, NULL);
}

re_err_code_t re_dbg_session_replay_close(re_dbg_session_t *s) {
    if (!s)
        return RE_E_INTERNAL;
    if (s->trace)
        re_dbg_trace_clear(s->trace);
    s->offline_replay = false;
    s->replay_path[0] = 0;
    return note(s, RE_OK, NULL);
}

bool re_dbg_session_replay_active(const re_dbg_session_t *s) {
    return s && s->offline_replay;
}

const char *re_dbg_session_replay_path(const re_dbg_session_t *s) {
    if (!s || !s->offline_replay)
        return "";
    return s->replay_path;
}

re_err_code_t re_dbg_session_replay_frame(re_dbg_session_t *s, uint64_t index,
                                          re_dbg_trace_frame_t *out) {
    re_dbg_trace_t *t;
    size_t i;
    size_t start;
    if (!s || !out)
        return note(s, RE_E_USAGE, "replay.frame needs an output frame");
    t = s->trace;
    if (!t || t->count == 0)
        return note(s, RE_E_NOTFOUND, "no offline trace frames loaded");
    start = t->count < RE_TRACE_CAP ? 0 : t->head;
    for (i = 0; i < t->count; i++) {
        const re_dbg_trace_frame_t *f = &t->frames[(start + i) % RE_TRACE_CAP];
        if (f->index == index) {
            *out = *f;
            return note(s, RE_OK, NULL);
        }
    }
    return note(s, RE_E_NOTFOUND, "trace frame index not in buffer");
}

re_err_code_t re_dbg_session_trace_load(re_dbg_session_t *s, const char *path) {
    re_dbg_trace_t *t;
    re_arena_t scratch;
    re_file_t f;
    re_jr_t root;
    const re_jr_t *frames;
    const re_jr_t *kind;
    size_t i;
    re_err_code_t c;
    if (!s || !path || !path[0])
        return note(s, RE_E_USAGE, "traceload needs a path");
    t = s->trace;
    if (!t) {
        t = (re_dbg_trace_t *)re_arena_calloc(&s->arena, 1, sizeof(*t));
        if (!t)
            return note(s, RE_E_NOMEM, "trace buffer alloc failed");
        s->trace = t;
    }
    re_arena_init(&scratch, 512u * 1024u);
    c = re_file_open(path, &scratch, &f);
    if (c != RE_OK) {
        re_arena_free(&scratch);
        return note(s, c, "traceload open failed");
    }
    if (!re_jr_parse(&scratch, (const char *)f.whole.p, f.whole.n, &root) ||
        root.kind != RE_JR_OBJ) {
        re_file_close(&f);
        re_arena_free(&scratch);
        return note(s, RE_E_MALFORMED, "traceload JSON parse failed");
    }
    kind = re_jr_get(&root, "kind");
    frames = re_jr_get(&root, "frames");
    if (!kind || kind->kind != RE_JR_STR || !re_str_eq_cstr(kind->str, "trace") || !frames ||
        frames->kind != RE_JR_ARR) {
        re_file_close(&f);
        re_arena_free(&scratch);
        return note(s, RE_E_MALFORMED, "traceload expects kind=trace frames[]");
    }
    re_dbg_trace_clear(t);
    t->dropped = parse_u64(re_jr_get(&root, "dropped"));
    for (i = 0; i < frames->count; i++)
        load_one_frame(t, &frames->items[i]);
    t->head = t->count % RE_TRACE_CAP;
    t->recording = false;
    /* Bare traceload is not offline MCP mode; replay.open sets the flag. */
    s->offline_replay = false;
    s->replay_path[0] = 0;
    re_file_close(&f);
    re_arena_free(&scratch);
    return note(s, RE_OK, NULL);
}
