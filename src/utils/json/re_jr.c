// re_jr.c - a minimal JSON reader.
// Module: util (C11).
// Owns: parsing bytes into a read only document.
// Depends: re_jr.h, re_arena, re_strbuf. Never reads past the span it was given, which
//           matters because the bytes came off a socket from something we do not trust.
#include "utils/json/re_jr.h"

#include "utils/mem/re_arena.h"
#include "utils/text/re_strbuf.h"

#include <string.h>

// The parse state. Kept as one struct rather than a parameter list, because every
// level of the grammar needs the same three things and passing them through by hand is
// how a bounds check gets dropped at depth.
typedef struct {
    const char *p;
    const char *end;
    re_arena_t *a;
    int depth;
} jr_t;

#define JR_MAX_DEPTH 32 // a request is a few levels deep; this refuses a bomb

static void skip_ws(jr_t *j) {
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r'))
        j->p++;
}

static bool parse_value(jr_t *j, re_jr_t *out);

// Read a string body into the arena. The cursor is left after the closing quote, so the
// caller sees a consumed token rather than having to find it again.
static bool parse_string(jr_t *j, re_str_t *out) {
    if (j->p >= j->end || *j->p != '"')
        return false;
    j->p++;
    const char *start = j->p;
    while (j->p < j->end && *j->p != '"') {
        if (*j->p == '\\') {
            j->p++;
            if (j->p >= j->end)
                return false;
        }
        j->p++;
    }
    if (j->p >= j->end)
        return false;
    size_t n = (size_t)(j->p - start);
    j->p++; // the closing quote
    out->p = (const char *)re_arena_memdup(j->a, start, n);
    out->n = (uint32_t)n;
    return out->p != NULL;
}

// An escaped string still needs its escapes resolved, because a request sends real
// paths with backslashes and "\\" is two characters on the wire. Resolving into a second
// buffer is simpler than in place and the request is small.
static bool unescape_into(re_strbuf_t *b, re_str_t s) {
    for (size_t i = 0; i < s.n; i++) {
        if (s.p[i] != '\\' || i + 1 >= s.n) {
            if (!re_strbuf_putc(b, s.p[i]))
                return false;
            continue;
        }
        i++;
        char c = s.p[i];
        switch (c) {
            case 'n':
                re_strbuf_putc(b, '\n');
                break;
            case 't':
                re_strbuf_putc(b, '\t');
                break;
            case 'r':
                re_strbuf_putc(b, '\r');
                break;
            case 'b':
                re_strbuf_putc(b, '\b');
                break;
            case 'f':
                re_strbuf_putc(b, '\f');
                break;
            case 'u': {
                // Only the ASCII range is resolved. A \u escape outside it is kept as
                // the literal characters, because decoding to UTF-8 would need a table
                // and a request field is never one that needs it.
                unsigned code = 0;
                for (int k = 0; k < 4 && i + 1 < s.n; k++) {
                    char h = s.p[++i];
                    int d = (h >= '0' && h <= '9')   ? h - '0'
                            : (h >= 'a' && h <= 'f') ? h - 'a' + 10
                            : (h >= 'A' && h <= 'F') ? h - 'A' + 10
                                                     : -1;
                    if (d < 0)
                        return false;
                    code = code * 16u + (unsigned)d;
                }
                if (code < 0x80u)
                    re_strbuf_putc(b, (char)code);
                else
                    re_strbuf_putc(b, '?');
                break;
            }
            case '/':
                re_strbuf_putc(b, '/');
                break;
            case '"':
            case '\\':
                re_strbuf_putc(b, c);
                break;
            default:
                // An escape JSON does not define is refused rather than passed
                // through. A sender that meant something else by it has a bug, and
                // silently decoding it to the character itself would hide that here
                // and surface it much later as a wrong argument.
                return false;
        }
    }
    return true;
}

static bool parse_number(jr_t *j, re_jr_t *out) {
    const char *start = j->p;
    if (j->p < j->end && (*j->p == '-' || *j->p == '+'))
        j->p++;
    bool any = false;
    bool leading_zero = false;
    if (j->p < j->end && *j->p == '0') {
        any = true;
        leading_zero = true;
        j->p++;
    }
    while (j->p < j->end && *j->p >= '0' && *j->p <= '9') {
        // JSON has no leading zeros: 01 is two tokens, not the number one. Accepting
        // it here would mean a frame we read is not the frame that was sent, and this
        // reader exists precisely to say whether that is true.
        if (leading_zero)
            return false;
        j->p++;
        any = true;
    }
    if (j->p < j->end && *j->p == '.') {
        j->p++;
        while (j->p < j->end && *j->p >= '0' && *j->p <= '9') {
            j->p++;
            any = true;
        }
    }
    if (j->p < j->end && (*j->p == 'e' || *j->p == 'E')) {
        j->p++;
        if (j->p < j->end && (*j->p == '-' || *j->p == '+'))
            j->p++;
        while (j->p < j->end && *j->p >= '0' && *j->p <= '9')
            j->p++;
    }
    if (!any)
        return false;
    size_t n = (size_t)(j->p - start);
    char tmp[64];
    if (n >= sizeof(tmp))
        return false; // far past any number a protocol field carries
    memcpy(tmp, start, n);
    tmp[n] = '\0';
    out->kind = RE_JR_NUM;
    out->raw.p = (const char *)re_arena_memdup(j->a, start, n);
    out->raw.n = (uint32_t)n;
    out->num = 0.0; // the text in raw is authoritative; the double is a convenience
    return true;
}

// An object or an array. Both are a sequence of values, and both are bounded, so one
// function does both and the kind is decided by the opening bracket.
static bool parse_seq(jr_t *j, re_jr_t *out, bool is_obj) {
    char open = is_obj ? '{' : '[';
    char close = is_obj ? '}' : ']';
    if (j->p >= j->end || *j->p != open)
        return false;
    j->p++;
    out->kind = is_obj ? RE_JR_OBJ : RE_JR_ARR;
    out->count = 0;
    out->items = (re_jr_t *)re_arena_alloc(j->a, sizeof(re_jr_t) * RE_JR_MAX_MEMBERS);
    if (is_obj)
        out->keys = (re_str_t *)re_arena_alloc(j->a, sizeof(re_str_t) * RE_JR_MAX_MEMBERS);
    if (!out->items || (is_obj && !out->keys))
        return false;
    skip_ws(j);
    if (j->p < j->end && *j->p == close) {
        j->p++;
        return true;
    }
    for (;;) {
        skip_ws(j);
        if (is_obj) {
            if (!parse_string(j, &out->keys[out->count]))
                return false;
            skip_ws(j);
            if (j->p >= j->end || *j->p != ':')
                return false;
            j->p++;
        }
        skip_ws(j);
        if (out->count >= RE_JR_MAX_MEMBERS)
            return false; // more members than anything we send, so it is not ours
        if (!parse_value(j, &out->items[out->count]))
            return false;
        out->count++;
        skip_ws(j);
        if (j->p < j->end && *j->p == ',') {
            j->p++;
            continue;
        }
        if (j->p < j->end && *j->p == close) {
            j->p++;
            return true;
        }
        return false;
    }
}

static bool parse_value(jr_t *j, re_jr_t *out) {
    if (++j->depth > JR_MAX_DEPTH)
        return false;
    skip_ws(j);
    bool ok = false;
    if (j->p >= j->end) {
        j->depth--;
        return false;
    }
    char c = *j->p;
    if (c == '{')
        ok = parse_seq(j, out, true);
    else if (c == '[')
        ok = parse_seq(j, out, false);
    else if (c == '"') {
        re_str_t raw = {0};
        if (parse_string(j, &raw)) {
            re_strbuf_t b;
            re_strbuf_init(&b, j->a);
            if (unescape_into(&b, raw)) {
                out->kind = RE_JR_STR;
                out->str.p = b.p ? b.p : "";
                out->str.n = (uint32_t)b.len;
                ok = true;
            }
        }
    } else if (c == 't' && (size_t)(j->end - j->p) >= 4 && memcmp(j->p, "true", 4) == 0) {
        j->p += 4;
        out->kind = RE_JR_BOOL;
        out->boolean = true;
        ok = true;
    } else if (c == 'f' && (size_t)(j->end - j->p) >= 5 && memcmp(j->p, "false", 5) == 0) {
        j->p += 5;
        out->kind = RE_JR_BOOL;
        out->boolean = false;
        ok = true;
    } else if (c == 'n' && (size_t)(j->end - j->p) >= 4 && memcmp(j->p, "null", 4) == 0) {
        j->p += 4;
        out->kind = RE_JR_NULL;
        ok = true;
    } else {
        ok = parse_number(j, out);
    }
    j->depth--;
    if (!ok)
        out->kind = RE_JR_BAD;
    return ok;
}

bool re_jr_parse(re_arena_t *a, const char *src, size_t n, re_jr_t *out) {
    jr_t j;
    j.p = src;
    j.end = src + n;
    j.a = a;
    j.depth = 0;
    memset(out, 0, sizeof(*out));
    if (!parse_value(&j, out))
        return false;
    // A frame carries exactly one message. Anything after it means the stream is out
    // of step, and refusing is better than reading a request and dropping the rest.
    skip_ws(&j);
    return j.p == j.end;
}

const re_jr_t *re_jr_get(const re_jr_t *obj, const char *key) {
    if (!obj || obj->kind != RE_JR_OBJ)
        return NULL;
    re_str_t want = re_str(key);
    for (size_t i = 0; i < obj->count; i++)
        if (re_str_eq(obj->keys[i], want))
            return &obj->items[i];
    return NULL;
}

bool re_jr_has(const re_jr_t *obj, const char *key) {
    return re_jr_get(obj, key) != NULL;
}

re_str_t re_jr_str(const re_jr_t *v, const char *fallback) {
    if (!v || v->kind != RE_JR_STR)
        return re_str(fallback);
    return v->str;
}

int64_t re_jr_i64(const re_jr_t *v, int64_t fallback) {
    if (!v || v->kind != RE_JR_NUM)
        return fallback;
    // The raw text is used rather than the double, because an address does not fit in
    // a double exactly and silently losing the low bits of a pointer is the kind of
    // bug that looks like a data error in the target file.
    int64_t out = 0;
    bool neg = false;
    size_t i = 0;
    if (v->raw.n && (v->raw.p[0] == '-' || v->raw.p[0] == '+')) {
        neg = v->raw.p[0] == '-';
        i = 1;
    }
    for (; i < v->raw.n && v->raw.p[i] >= '0' && v->raw.p[i] <= '9'; i++)
        out = out * 10 + (v->raw.p[i] - '0');
    return neg ? -out : out;
}

bool re_jr_bool(const re_jr_t *v, bool fallback) {
    if (!v || v->kind != RE_JR_BOOL)
        return fallback;
    return v->boolean;
}

void re_jr_escape(re_strbuf_t *out, re_str_t s) {
    re_strbuf_putc(out, '"');
    for (size_t i = 0; i < s.n; i++) {
        char c = s.p[i];
        if (c == '"' || c == '\\') {
            re_strbuf_putc(out, '\\');
            re_strbuf_putc(out, c);
        } else if (c == '\n') {
            re_strbuf_puts(out, "\\n");
        } else if (c == '\t') {
            re_strbuf_puts(out, "\\t");
        } else if (c == '\r') {
            re_strbuf_puts(out, "\\r");
        } else if ((unsigned char)c < 0x20) {
            re_strbuf_appendf(out, "\\u%04x", (unsigned)(unsigned char)c);
        } else {
            re_strbuf_putc(out, c);
        }
    }
    re_strbuf_putc(out, '"');
}
