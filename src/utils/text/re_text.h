// re_text.h - the human readable renderer. The other sanctioned stdout writer.
// Module: util (C11).
// Owns: column aligned tables, so --format text stays readable without a DOM.
// Depends: re_strbuf, re_fmt, re_str. Never touches JSON, never writes stderr.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#include <stdint.h>

#include "utils/text/re_strbuf.h"

typedef struct {
    re_strbuf_t *out;
    size_t widths[16];
    size_t n_cols;
} re_text_t;

void re_text_init(re_text_t *t, re_strbuf_t *out);
// Update column widths without printing. Call this over every row before the
// first re_text_header when alignment matters, so a data cell wider than its
// header cannot underflow the padding arithmetic.
void re_text_measure(re_text_t *t, const char *const *cells, size_t n);
void re_text_header(re_text_t *t, const char *const *cols, size_t n);
void re_text_row(re_text_t *t, const char *const *cells, size_t n);
void re_text_flush(re_text_t *t);
#ifdef __cplusplus
}
#endif
