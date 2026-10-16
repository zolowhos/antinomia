// re_tui_text.c - measuring and clipping text to a column budget.
// Module: util (C11).
// Owns: display width, clipping, padding and word wrap.
// Depends: re_tui.h. The rule everywhere is that a box must keep its shape, so text
// is cut to fit rather than allowed to spill and move the frame.
#include "utils/tui/re_tui.h"

#include <string.h>

// Decode the code point starting at p, given n bytes available. Returns the bytes
// consumed and stores the value. A malformed sequence decodes to the replacement
// value and consumes one byte, so a corrupt name cannot stall the walk.
static size_t decode(const unsigned char *p, size_t n, uint32_t *out) {
    unsigned char c = p[0];
    if ((c & 0x80u) == 0) {
        *out = c;
        return 1;
    }
    if ((c & 0xE0u) == 0xC0u && n >= 2) {
        *out = (uint32_t)(c & 0x1Fu) << 6 | (uint32_t)(p[1] & 0x3Fu);
        return 2;
    }
    if ((c & 0xF0u) == 0xE0u && n >= 3) {
        *out =
            (uint32_t)(c & 0x0Fu) << 12 | (uint32_t)(p[1] & 0x3Fu) << 6 | (uint32_t)(p[2] & 0x3Fu);
        return 3;
    }
    if ((c & 0xF8u) == 0xF0u && n >= 4) {
        *out = (uint32_t)(c & 0x07u) << 18 | (uint32_t)(p[1] & 0x3Fu) << 12 |
               (uint32_t)(p[2] & 0x3Fu) << 6 | (uint32_t)(p[3] & 0x3Fu);
        return 4;
    }
    // A lead byte that announces more bytes than are there is one damaged character,
    // not several. It is counted once, so a truncated sequence cannot make a box too
    // wide and push the border out of shape.
    *out = 0xFFFDu;
    if ((c & 0xE0u) == 0xC0u)
        return n >= 2 ? 2 : 1;
    if ((c & 0xF0u) == 0xE0u)
        return n >= 3 ? 3 : (n >= 2 ? 2 : 1);
    if ((c & 0xF8u) == 0xF0u)
        return n >= 4 ? 4 : (n >= 3 ? 3 : (n >= 2 ? 2 : 1));
    return 1;
}

// A code point that occupies no column of its own. This covers the combining diacritical
// marks block, which is what an accented symbol name is made of and therefore the case
// that actually shows up here. It is deliberately not a full Unicode width
// implementation: East Asian wide characters are counted as one column, which is wrong
// for CJK text and is a known limitation rather than a claim to be correct.
static bool zero_width(uint32_t cp) {
    return cp >= 0x0300u && cp <= 0x036Fu;
}

size_t re_tui_cols(const char *s) {
    return s ? re_tui_cols_span(s, strlen(s)) : 0;
}

size_t re_tui_cols_span(const char *s, size_t n) {
    size_t cols = 0;
    size_t i = 0;
    if (!s)
        return 0;
    while (i < n) {
        uint32_t cp = 0;
        size_t len = decode((const unsigned char *)s + i, n - i, &cp);
        if (!zero_width(cp))
            cols++;
        i += len;
    }
    return cols;
}

bool re_tui_fits_panels(const re_tui_t *t, size_t n, size_t min_w) {
    if (!n)
        return true;
    return t->width >= n * min_w + (n - 1);
}

// Is this the start of an escape sequence? Only the SGR form is ever emitted here, so
// the test is cheap and exact rather than a general escape parser.
static bool is_escape(const char *s, size_t n, size_t at) {
    return at + 1 < n && s[at] == '\x1b' && s[at + 1] == '[';
}

// The length of the escape sequence at s[at], or 0 when there is none. A sequence
// that runs off the end of the span is treated as absent, so a truncated one cannot
// send the walk past the end.
static size_t escape_len(const char *s, size_t n, size_t at) {
    if (!is_escape(s, n, at))
        return 0;
    for (size_t i = at + 2; i < n; i++) {
        if (s[i] == 'm')
            return i - at + 1;
    }
    return 0;
}

// The display columns of a line that may contain escapes.
//
// This is the primitive the panel composition actually needs, because a panel body is
// filled with styled text and the escapes sit in the same bytes as the content. A
// measurement that counted them would see a line as far wider than it looks, clip it
// to fit, and cut a value in half in the middle of a colour change. The escapes are
// zero width by definition, so they are skipped.
size_t re_tui_cols_line(const char *s, size_t n) {
    size_t cols = 0;
    size_t i = 0;
    if (!s)
        return 0;
    while (i < n) {
        size_t esc = escape_len(s, n, i);
        if (esc) {
            i += esc;
            continue;
        }
        uint32_t cp = 0;
        size_t len = decode((const unsigned char *)s + i, n - i, &cp);
        if (!zero_width(cp))
            cols++;
        i += len;
    }
    return cols;
}

// Copy a possibly styled line into dst, clipped to a display budget, escapes intact
// and an ellipsis when it did not fit. The escapes are copied through rather than
// dropped, because dropping the closing reset would let a colour run past the value it
// belonged to and colour the border.
void re_tui_clip_line(re_strbuf_t *dst, const re_tui_t *t, const char *s, size_t n, size_t budget) {
    if (!s || !n || !budget)
        return;
    if (re_tui_cols_line(s, n) <= budget) {
        re_strbuf_append(dst, s, n);
        return;
    }
    size_t room = budget - 1;
    size_t cols = 0;
    size_t i = 0;
    while (i < n) {
        size_t esc = escape_len(s, n, i);
        if (esc) {
            re_strbuf_append(dst, s + i, esc);
            i += esc;
            continue;
        }
        uint32_t cp = 0;
        size_t len = decode((const unsigned char *)s + i, n - i, &cp);
        re_strbuf_append(dst, s + i, len);
        if (!zero_width(cp))
            cols++;
        i += len;
        if (cols >= room)
            break;
    }
    re_strbuf_puts(dst, t->unicode ? "\xe2\x80\xa6" : "~");
}

void re_tui_clip_span(re_strbuf_t *dst, const re_tui_t *t, const char *s, size_t n, size_t budget) {
    if (!s || !n || !budget)
        return;
    if (re_tui_cols_span(s, n) <= budget) {
        re_strbuf_append(dst, s, n);
        return;
    }
    // Leave one column for the ellipsis, so a shortened value looks shortened rather
    // than looking like a complete one that happens to end there.
    size_t room = budget - 1;
    size_t cols = 0;
    size_t i = 0;
    while (i < n && cols < room) {
        uint32_t cp = 0;
        size_t len = decode((const unsigned char *)s + i, n - i, &cp);
        re_strbuf_append(dst, s + i, len);
        // A combining mark is copied but not counted, so the sequence it belongs to
        // still reaches the output intact.
        if (!zero_width(cp))
            cols++;
        i += len;
    }
    re_strbuf_puts(dst, t->unicode ? "\xe2\x80\xa6" : "~");
}

void re_tui_pad_span(re_strbuf_t *dst, const re_tui_t *t, const char *s, size_t n, size_t width) {
    size_t cols = re_tui_cols_span(s, n);
    re_tui_clip_span(dst, t, s, n, width);
    while (cols < width) {
        re_strbuf_putc(dst, ' ');
        cols++;
    }
}

void re_tui_clip(re_strbuf_t *dst, const re_tui_t *t, const char *s, size_t budget) {
    re_tui_clip_span(dst, t, s, s ? strlen(s) : 0, budget);
}

void re_tui_pad(re_strbuf_t *dst, const re_tui_t *t, const char *s, size_t width) {
    re_tui_pad_span(dst, t, s, s ? strlen(s) : 0, width);
}

void re_tui_bar(re_strbuf_t *dst, const re_tui_t *t, double frac, size_t cells) {
    if (frac < 0.0)
        frac = 0.0;
    if (frac > 1.0)
        frac = 1.0;
    if (!cells)
        return;
    // Eighth blocks, so the bar has some resolution without needing a character set
    // that is not ASCII. The ascii fallback is a filled and an empty hash rather than
    // a bar of equal signs, so a bar still reads as a bar and not as a dashed rule.
    static const char *const kEighth[8] = {
        "\xe2\x96\x88", "\xe2\x96\x89", "\xe2\x96\x8a", "\xe2\x96\x8b",
        "\xe2\x96\x8c", "\xe2\x96\x8d", "\xe2\x96\x8e", "\xe2\x96\x8f",
    };
    static const char kEmpty[] = "\xe2\x96\x91";
    static const char kAsciiFull[] = "#";
    static const char kAsciiEmpty[] = ".";
    if (!t->unicode) {
        // The ascii fallback has no eighth blocks, so it quantises to whole cells.
        // Printing one character per eighth here would make the bar eight times too
        // wide and would not be a bar at all.
        size_t full = (size_t)(frac * (double)cells + 0.5);
        if (full > cells)
            full = cells;
        for (size_t i = 0; i < full; i++)
            re_strbuf_puts(dst, kAsciiFull);
        for (size_t i = full; i < cells; i++)
            re_strbuf_puts(dst, kAsciiEmpty);
        return;
    }
    size_t eighths = (size_t)(frac * (double)(cells * 8) + 0.5);
    if (eighths > cells * 8)
        eighths = cells * 8;
    for (size_t i = 0; i < eighths; i++)
        re_strbuf_puts(dst, kEighth[i % 8]);
    for (size_t i = eighths; i < cells * 8; i += 8)
        re_strbuf_puts(dst, kEmpty);
}
