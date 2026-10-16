// re_script.c - brace format and session notes/log/trace ring helpers.
// Module: feature/script (C11).
// Owns: {expr} expansion, note/log append, trace push/list/clear.
// Depends: re_script.h, re_expr, re_str.
#include "features/script/re_script.h"

#include "features/expr/re_expr.h"
#include "utils/text/re_str.h"

#include <string.h>

static void copy_str(char *dst, size_t cap, const char *src) {
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

static bool putc_out(char *out, size_t cap, size_t *n, char c) {
    if (!out || !n || *n + 1u >= cap)
        return false;
    out[(*n)++] = c;
    out[*n] = 0;
    return true;
}

static bool put_hex(char *out, size_t cap, size_t *n, uint64_t v) {
    char dig[16];
    size_t j;
    size_t start = 0;
    for (j = 0; j < 16; j++) {
        unsigned shift = (unsigned)((15u - (unsigned)j) * 4u);
        unsigned nibble = (unsigned)((v >> shift) & 0xfu);
        dig[j] = (char)(nibble < 10u ? ('0' + (char)nibble) : ('a' + (char)(nibble - 10u)));
    }
    while (start + 1u < 16u && dig[start] == '0')
        start++;
    if (!putc_out(out, cap, n, '0') || !putc_out(out, cap, n, 'x'))
        return false;
    for (j = start; j < 16; j++) {
        if (!putc_out(out, cap, n, dig[j]))
            return false;
    }
    return true;
}

static bool expand_brace(re_dbg_session_t *s, const char *body, size_t body_n, char *out,
                         size_t cap, size_t *n) {
    char expr[RE_DBG_BP_COND_MAX];
    char err[RE_EXPR_ERR_MAX];
    int64_t v = 0;
    size_t i;
    if (body_n + 1u > sizeof(expr))
        return false;
    for (i = 0; i < body_n; i++)
        expr[i] = body[i];
    expr[body_n] = 0;
    if (re_expr_eval(s, expr, &v, err, sizeof(err)) != RE_OK)
        return false;
    return put_hex(out, cap, n, (uint64_t)v);
}

re_err_code_t re_script_format(re_dbg_session_t *s, const char *fmt, char *out, size_t cap) {
    size_t n = 0;
    const char *p;
    if (!fmt || !out || cap == 0)
        return RE_E_USAGE;
    out[0] = 0;
    p = fmt;
    while (*p) {
        const char *close;
        size_t body_n;
        if (*p != '{') {
            if (!putc_out(out, cap, &n, *p++))
                return RE_E_RANGE;
            continue;
        }
        close = p + 1;
        while (*close && *close != '}')
            close++;
        if (*close != '}') {
            if (!putc_out(out, cap, &n, *p++))
                return RE_E_RANGE;
            continue;
        }
        body_n = (size_t)(close - (p + 1));
        if (!expand_brace(s, p + 1, body_n, out, cap, &n)) {
            while (p <= close) {
                if (!putc_out(out, cap, &n, *p++))
                    return RE_E_RANGE;
            }
            continue;
        }
        p = close + 1;
    }
    return RE_OK;
}

static void ring_push(char *base, size_t row_len, size_t *n_io, size_t max, const char *text) {
    size_t i;
    size_t n = *n_io;
    if (n >= max) {
        for (i = 1; i < max; i++)
            memcpy(base + (i - 1u) * row_len, base + i * row_len, row_len);
        n = max - 1u;
    }
    copy_str(base + n * row_len, row_len, text);
    *n_io = n + 1u;
}

re_err_code_t re_dbg_session_note_add(re_dbg_session_t *s, const char *text) {
    char formatted[RE_DBG_NOTE_LEN];
    if (!s || !text)
        return RE_E_USAGE;
    if (re_script_format(s, text, formatted, sizeof(formatted)) != RE_OK)
        copy_str(formatted, sizeof(formatted), text);
    ring_push(&s->notes[0][0], RE_DBG_NOTE_LEN, &s->notes_n, RE_DBG_NOTE_MAX, formatted);
    return RE_OK;
}

re_err_code_t re_dbg_session_note_list(re_dbg_session_t *s, const char **out, size_t cap,
                                       size_t *n_out) {
    size_t i;
    if (n_out)
        *n_out = 0;
    if (!s)
        return RE_E_INTERNAL;
    for (i = 0; i < s->notes_n && out && i < cap; i++)
        out[i] = s->notes[i];
    if (n_out)
        *n_out = s->notes_n;
    return RE_OK;
}

re_err_code_t re_dbg_session_log_add(re_dbg_session_t *s, const char *text) {
    char formatted[RE_DBG_BP_COND_MAX];
    if (!s || !text)
        return RE_E_USAGE;
    if (re_script_format(s, text, formatted, sizeof(formatted)) != RE_OK)
        copy_str(formatted, sizeof(formatted), text);
    ring_push(&s->bp_log[0][0], RE_DBG_BP_COND_MAX, &s->bp_log_n, RE_DBG_BP_LOG_MAX, formatted);
    return RE_OK;
}

re_err_code_t re_dbg_session_log_list(re_dbg_session_t *s, const char **out, size_t cap,
                                      size_t *n_out) {
    size_t i;
    if (n_out)
        *n_out = 0;
    if (!s)
        return RE_E_INTERNAL;
    for (i = 0; i < s->bp_log_n && out && i < cap; i++)
        out[i] = s->bp_log[i];
    if (n_out)
        *n_out = s->bp_log_n;
    return RE_OK;
}

re_err_code_t re_dbg_session_traces_list(re_dbg_session_t *s, re_dbg_trace_rec_t *out, size_t cap,
                                        size_t *n_out) {
    size_t i;
    if (n_out)
        *n_out = 0;
    if (!s)
        return RE_E_INTERNAL;
    for (i = 0; i < s->trace_n && out && i < cap; i++)
        out[i] = s->traces[i];
    if (n_out)
        *n_out = s->trace_n;
    return RE_OK;
}

re_err_code_t re_dbg_session_traces_push(re_dbg_session_t *s, const re_dbg_trace_rec_t *rec) {
    size_t i;
    if (!s || !rec)
        return RE_E_USAGE;
    if (s->trace_n >= RE_DBG_TRACE_MAX) {
        for (i = 1; i < RE_DBG_TRACE_MAX; i++)
            s->traces[i - 1] = s->traces[i];
        s->trace_n = RE_DBG_TRACE_MAX - 1u;
    }
    s->traces[s->trace_n++] = *rec;
    return RE_OK;
}

re_err_code_t re_dbg_session_traces_clear(re_dbg_session_t *s) {
    if (!s)
        return RE_E_INTERNAL;
    s->trace_n = 0;
    memset(s->traces, 0, sizeof(s->traces));
    return RE_OK;
}

bool re_dbg_session_consume_pending_cmd(re_dbg_session_t *s, char *out, size_t cap) {
    if (!s || !s->pending_cmd[0])
        return false;
    if (out && cap)
        copy_str(out, cap, s->pending_cmd);
    s->pending_cmd[0] = 0;
    return true;
}
