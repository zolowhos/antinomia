// re_json.c - the streaming writer, kept small enough to audit in one sitting.
// Module: util (C11).
// Owns: nesting, comma placement, string escaping, and the depth overflow flag.
// Depends: re_json.h only. No globals beyond a static escape table.
#include "utils/json/re_json.h"

#include <stdio.h>

#include "utils/text/re_fmt.h"

static const char *const kHexDigits = "0123456789abcdef";

static void sep(re_jw_t *w) {
    if (w->depth > 0 && w->depth <= RE_JSON_MAX_DEPTH) {
        if (w->need_comma[w->depth])
            re_strbuf_putc(&w->buf, ',');
        w->need_comma[w->depth] = true;
    }
}

static void push(re_jw_t *w) {
    if (w->depth >= RE_JSON_MAX_DEPTH) {
        w->overflow = true;
        return;
    }
    w->need_comma[w->depth + 1] = false;
    w->depth++;
}

static void pop(re_jw_t *w) {
    if (w->depth > 0)
        w->depth--;
}

static void emit_escaped(re_jw_t *w, re_str_t v) {
    re_strbuf_putc(&w->buf, '"');
    for (size_t i = 0; i < v.n; i++) {
        unsigned char c = (unsigned char)v.p[i];
        switch (c) {
            case '"':
                re_strbuf_puts(&w->buf, "\\\"");
                break;
            case '\\':
                re_strbuf_puts(&w->buf, "\\\\");
                break;
            case '\n':
                re_strbuf_puts(&w->buf, "\\n");
                break;
            case '\r':
                re_strbuf_puts(&w->buf, "\\r");
                break;
            case '\t':
                re_strbuf_puts(&w->buf, "\\t");
                break;
            case '\b':
                re_strbuf_puts(&w->buf, "\\b");
                break;
            case '\f':
                re_strbuf_puts(&w->buf, "\\f");
                break;
            default:
                if (c < 0x20)
                    re_strbuf_appendf(&w->buf, "\\u00%c%c", kHexDigits[c >> 4],
                                      kHexDigits[c & 0x0f]);
                else
                    re_strbuf_putc(&w->buf, (char)c);
                break;
        }
    }
    re_strbuf_putc(&w->buf, '"');
}

void re_jw_init(re_jw_t *w, re_arena_t *a) {
    w->arena = a;
    w->depth = 0;
    w->overflow = false;
    w->need_comma[0] = false;
    re_strbuf_init(&w->buf, a);
}

bool re_jw_ok(const re_jw_t *w) {
    return !w->overflow && w->depth == 0;
}

void re_jw_obj(re_jw_t *w) {
    sep(w);
    re_strbuf_putc(&w->buf, '{');
    push(w);
}

void re_jw_obj_end(re_jw_t *w) {
    pop(w);
    re_strbuf_putc(&w->buf, '}');
}

void re_jw_arr(re_jw_t *w) {
    sep(w);
    re_strbuf_putc(&w->buf, '[');
    push(w);
}

void re_jw_arr_end(re_jw_t *w) {
    pop(w);
    re_strbuf_putc(&w->buf, ']');
}

void re_jw_key(re_jw_t *w, const char *key) {
    sep(w);
    re_strbuf_putc(&w->buf, '"');
    re_strbuf_puts(&w->buf, key);
    re_strbuf_puts(&w->buf, "\":");
    if (w->depth > 0)
        w->need_comma[w->depth] = false;
}

void re_jw_key_re_str(re_jw_t *w, re_str_t key) {
    sep(w);
    re_strbuf_putc(&w->buf, '"');
    re_strbuf_put_re_str(&w->buf, key);
    re_strbuf_puts(&w->buf, "\":");
    if (w->depth > 0)
        w->need_comma[w->depth] = false;
}

void re_jw_str(re_jw_t *w, re_str_t v) {
    sep(w);
    emit_escaped(w, v);
}

void re_jw_cstr(re_jw_t *w, const char *v) {
    re_jw_str(w, re_str(v));
}

void re_jw_u64(re_jw_t *w, uint64_t v) {
    sep(w);
    re_strbuf_put_u64(&w->buf, v);
}

void re_jw_i64(re_jw_t *w, int64_t v) {
    sep(w);
    re_fmt_put_i64(&w->buf, v);
}

void re_jw_hex(re_jw_t *w, uint64_t v, int digits) {
    sep(w);
    re_strbuf_putc(&w->buf, '"');
    re_strbuf_putc(&w->buf, '0');
    re_strbuf_putc(&w->buf, 'x');
    re_strbuf_put_hex64(&w->buf, v, digits);
    re_strbuf_putc(&w->buf, '"');
}

void re_jw_f64(re_jw_t *w, double v) {
    sep(w);
    re_fmt_put_f64(&w->buf, v);
}

void re_jw_bool(re_jw_t *w, bool v) {
    sep(w);
    re_strbuf_puts(&w->buf, v ? "true" : "false");
}

void re_jw_null(re_jw_t *w) {
    sep(w);
    re_strbuf_puts(&w->buf, "null");
}

void re_jw_kstr(re_jw_t *w, const char *key, re_str_t v) {
    re_jw_key(w, key);
    re_jw_str(w, v);
}

void re_jw_kcstr(re_jw_t *w, const char *key, const char *v) {
    re_jw_kstr(w, key, re_str(v));
}

void re_jw_ku64(re_jw_t *w, const char *key, uint64_t v) {
    re_jw_key(w, key);
    re_jw_u64(w, v);
}

void re_jw_ki64(re_jw_t *w, const char *key, int64_t v) {
    re_jw_key(w, key);
    re_jw_i64(w, v);
}

void re_jw_khex(re_jw_t *w, const char *key, uint64_t v, int digits) {
    re_jw_key(w, key);
    re_jw_hex(w, v, digits);
}

void re_jw_kf64(re_jw_t *w, const char *key, double v) {
    re_jw_key(w, key);
    re_jw_f64(w, v);
}

void re_jw_kbool(re_jw_t *w, const char *key, bool v) {
    re_jw_key(w, key);
    re_jw_bool(w, v);
}

void re_jw_knull(re_jw_t *w, const char *key) {
    re_jw_key(w, key);
    re_jw_null(w);
}

void re_jw_ku64_array(re_jw_t *w, const char *key, const uint64_t *v, size_t n) {
    re_jw_key(w, key);
    re_jw_arr(w);
    for (size_t i = 0; i < n; i++)
        re_jw_u64(w, v[i]);
    re_jw_arr_end(w);
}

void re_jw_flush(re_jw_t *w, void *stream) {
    FILE *f = (FILE *)stream;
    char *s = re_strbuf_detach(&w->buf);
    if (s)
        fputs(s, f);
    fputc('\n', f);
    fflush(f);
}
