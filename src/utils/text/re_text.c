// re_text.c - fixed width table rendering, deliberately simple and allocation free.
// Module: util (C11).
// Owns: column width measurement, padding and row emission.
// Depends: re_text.h only. No globals, no state carried between tables.
#include "utils/text/re_text.h"

#include <string.h>

void re_text_init(re_text_t *t, re_strbuf_t *out) {
    t->out = out;
    t->n_cols = 0;
    for (int i = 0; i < 16; i++)
        t->widths[i] = 0;
}

void re_text_measure(re_text_t *t, const char *const *cells, size_t n) {
    if (n > 16)
        n = 16;
    for (size_t i = 0; i < n; i++) {
        size_t len = cells[i] ? strlen(cells[i]) : 0;
        if (len > t->widths[i])
            t->widths[i] = len;
    }
    if (n > t->n_cols)
        t->n_cols = n;
}

void re_text_header(re_text_t *t, const char *const *cols, size_t n) {
    re_text_measure(t, cols, n);
    for (size_t i = 0; i < n; i++) {
        if (i)
            re_strbuf_putc(t->out, ' ');
        re_strbuf_puts(t->out, cols[i] ? cols[i] : "");
    }
    re_strbuf_putc(t->out, '\n');
}

void re_text_row(re_text_t *t, const char *const *cells, size_t n) {
    re_text_measure(t, cells, n);
    if (n > 16)
        n = 16;
    for (size_t i = 0; i < n; i++) {
        if (i)
            re_strbuf_putc(t->out, ' ');
        const char *c = cells[i] ? cells[i] : "";
        re_strbuf_puts(t->out, c);
        size_t len = strlen(c);
        // Guarded: widths grow as rows are measured, so a cell can still be
        // wider than the column if the caller skipped the pre-measure pass.
        if (i + 1 < n && len < t->widths[i] + 1) {
            size_t pad = t->widths[i] + 1 - len;
            for (size_t k = 0; k < pad; k++)
                re_strbuf_putc(t->out, ' ');
        }
    }
    re_strbuf_putc(t->out, '\n');
}

void re_text_flush(re_text_t *t) {
    t->n_cols = 0;
}
