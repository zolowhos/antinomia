// re_syntax.h - paint one line of C-like pseudocode in more than one colour.
// Module: util (C11).
// Owns: which token gets which style. Writes cells and nothing else.
// Depends: re_screen. The emitter's text is ASCII, so one byte is one column.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#include <stdint.h>

#include "utils/tui/re_screen.h"

// Write line onto one row, from x up to limit, with a style per token. A keyword,
// a type, a comment, a string, a number, a label and a call each take their own
// colour. Everything else stays plain, which is what makes the coloured tokens
// readable rather than a second rainbow.
uint16_t re_syntax_put(re_screen_t *s, uint16_t y, uint16_t x, uint16_t limit, const char *line);

#ifdef __cplusplus
}
#endif
