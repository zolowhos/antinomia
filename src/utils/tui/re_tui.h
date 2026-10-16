// re_tui.h - the graphical looking layer: headers, rules, panels and boxes.
// Module: util (C11).
// Owns: everything about how a report looks. No data, no analysis, no I/O policy.
// Depends: re_strbuf, re_arena. Writes only into a caller's buffer.
//
// Why this exists beside re_text: re_text aligns a table and stops there, which is
// right for a log line and wrong for a report a person is meant to read. This adds
// the frame around it, the same way a terminal UI library would, and keeps the same
// property that matters most here: no dependency, and a plain fallback when the
// output is not a terminal.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "utils/mem/re_arena.h"
#include "utils/text/re_strbuf.h"

#define RE_TUI_MAX_PANELS 4
#define RE_TUI_MAX_COLS 16
#define RE_TUI_MIN_WIDTH 40u // below this a box cannot hold a key and a value
#define RE_TUI_DEFAULT_WIDTH 80u

// Style names, not escape sequences, so the escape table lives in one place and a
// caller never writes an escape by hand.
typedef enum {
    RE_ST_NONE = 0,
    RE_ST_TITLE,  // the header bar
    RE_ST_HEAD,   // a column heading
    RE_ST_LABEL,  // a key in a key/value pair
    RE_ST_ACCENT, // the selected item, or the one that matters
    RE_ST_GOOD,   // a finding that is expected
    RE_ST_WARN,   // something worth a second look
    RE_ST_BAD,    // something wrong
    RE_ST_HOVER,  // light blue background: the pointer is over a control
    RE_ST_PRESS,  // dark blue background: the button is held down
    RE_ST_KW,     // a keyword in pseudocode
    RE_ST_TYPE,   // a type name in pseudocode
    RE_ST_CMT,    // a comment in pseudocode
    RE_ST_STR,    // a string literal in pseudocode
    RE_ST_NUM,    // a number in pseudocode
    RE_ST_CALL,   // a call target in pseudocode
} re_style_t;

typedef struct {
    re_strbuf_t *out;
    re_strbuf_t *scratch; // for building a styled run before styling it
    uint16_t width;       // terminal columns to lay out into
    bool color;           // emit ANSI escapes
    bool unicode;         // draw boxes with UTF-8 line drawing
} re_tui_t;

// One bordered box of a report. A panel is filled with content first and composed
// afterwards, because a row of panels has to be written line by line across all of
// them, and that is far harder to do while each is being written.
typedef struct {
    re_strbuf_t body;
    char title[32];
    uint16_t weight; // relative share of the row's width
} re_panel_t;

// Is stdout a terminal? This is the whole of the output policy: a person gets the
// framed report, a pipe gets the machine format. Nothing else decides.
bool re_tui_is_tty(void);

// Is stdin a terminal? The interactive shell needs this as well as stdout: a shell
// with a redirected stdin has nothing to read and would either spin or exit at once,
// which is the behaviour the shell exists to avoid.
bool re_tui_stdin_tty(void);

// The terminal width from the environment, or RE_TUI_DEFAULT_WIDTH. The
// environment rather than an ioctl, so this stays portable and testable.
uint16_t re_tui_term_width(void);

// Both dimensions from the terminal itself, which is the only source that follows a
// resize. Falls back to 80x24 when there is no terminal to ask, so a caller always gets
// a usable pair and never has to invent one. A full screen needs both, and there
// is no honest way to derive one from the other.
void re_tui_term_size(uint16_t *rows, uint16_t *cols);

// Whether colour is wanted: the caller passes what it decided, and this only
// resolves the NO_COLOR convention, which is a near universal expectation.
bool re_tui_want_color(void);

// Whether the output encoding can carry the box drawing characters. Ascii fallback
// exists because a report that is all mojibake is worse than one with plain dashes.
bool re_tui_want_unicode(void);

// Set the stream mode for this process. Binary is required by the MCP transport,
// which specifies one JSON object followed by a single \n and nothing else, and is
// wrong for a terminal, where a bare \n does not return the cursor to column 0 and
// every line after the first would be staircased. The caller says which it is rather
// than this guessing, because only the caller knows the protocol.
void re_tui_set_stream_mode(bool binary);

// True when the console can be trusted with the box characters: not a pipe, and on
// Windows a console whose output code page is UTF-8. The code page is read rather
// than set, because changing it would be a side effect on the user's shell, which is
// not this program's to impose.
bool re_tui_console_utf8(void);

// scratch is a buffer the header borrows to build a styled run. It must outlive the
// re_tui_t and needs no initialisation, because every use clears it first.
void re_tui_init(re_tui_t *t, re_strbuf_t *out, re_strbuf_t *scratch, bool color, bool unicode,
                 uint16_t width);

// The number of display columns a string occupies. Counts UTF-8 sequences once and
// ignores a combining mark, so a box does not drift when a name is not ASCII.
size_t re_tui_cols(const char *s);

// The same, over a span that is not NUL terminated. A composed row is made of spans,
// not strings, so measuring has to work on both.
size_t re_tui_cols_span(const char *s, size_t n);

// The display columns of a line that may contain colour escapes, and a clip that keeps
// them. A panel body is filled with styled text, so the escapes sit in the same bytes
// as the content and any measurement that counted them would see a line as far wider
// than it looks and cut a value in half in the middle of a colour change. These skip
// the escapes instead. A truncated escape is treated as absent rather than trusted.
size_t re_tui_cols_line(const char *s, size_t n);
void re_tui_clip_line(re_strbuf_t *dst, const re_tui_t *t, const char *s, size_t n, size_t budget);

// Whether n panels of at least min_w columns each fit in the current width. A caller
// with a narrow terminal uses this to decide to show fewer of them, which is the
// honest response: shrinking them all to fit produces boxes too narrow to read.
bool re_tui_fits_panels(const re_tui_t *t, size_t n, size_t min_w);

// Every primitive that writes takes the buffer to write into, rather than assuming
// the report's own output. This is not a stylistic choice: a panel is filled before
// it is composed, so its content has to go somewhere that is not the report yet, and
// a primitive that hard codes the destination silently sends a panel's contents
// straight to stdout. t is passed for the settings, not for the destination.
void re_tui_styled(re_strbuf_t *dst, const re_tui_t *t, re_style_t style, const char *s);

// The escape that turns a style on, or an empty string for none and for a style
// this table does not know. A painter emits a reset first, then this.
const char *re_tui_style_on(re_style_t style);
void re_tui_fill(re_strbuf_t *dst, const re_tui_t *t, size_t n, const char *with);

// Write s clipped to a column budget, with a one character ellipsis when it does not
// fit. Clip on a character boundary, never mid sequence, or the output is not
// decodable.
//
// The span forms take an explicit length and are the primitives: a composed row is
// made of spans into a shared body, not of NUL terminated strings, and a version that
// only took a string would read past the end of a line into the rest of the panel.
void re_tui_clip_span(re_strbuf_t *dst, const re_tui_t *t, const char *s, size_t n, size_t budget);
void re_tui_pad_span(re_strbuf_t *dst, const re_tui_t *t, const char *s, size_t n, size_t width);
void re_tui_clip(re_strbuf_t *dst, const re_tui_t *t, const char *s, size_t budget);
void re_tui_pad(re_strbuf_t *dst, const re_tui_t *t, const char *s, size_t width);

// A full width horizontal rule.
void re_tui_rule(re_tui_t *t);

// A proportion as a bar of block characters, cells wide, in the same number of
// columns either way. A bar is the fastest way to see that one section dominates,
// which is the question an entropy table is usually asked.
void re_tui_bar(re_strbuf_t *dst, const re_tui_t *t, double frac, size_t cells);

// The bar across the top: the subject on the left, a summary on the right.
void re_tui_header(re_tui_t *t, const char *left, const char *right);

// A row of section names with one picked out, the way a tab bar reads. It is static
// output, so the row is a contents line: it names what the report below contains.
void re_tui_tabs(re_tui_t *t, const char *const *labels, size_t n, size_t active);

// A panel, ready to be filled.
void re_panel_init(re_panel_t *p, re_arena_t *a, const char *title, uint16_t weight);
void re_panel_close(re_panel_t *p);

// A key and its value on one line, the value styled. Keys are dim so the eye finds
// the values first, which is the opposite of a plain dump and is the point.
void re_panel_kv(re_tui_t *t, re_panel_t *p, const char *key, const char *val, re_style_t style);
void re_panel_text(re_tui_t *t, re_panel_t *p, const char *s);
void re_panel_rule(re_tui_t *t, re_panel_t *p);

// Lay the panels out side by side, each as wide as its weight allows, all the same
// height, with a column of space between them. One panel composes into a single full
// width box, so there is no separate single box call to keep consistent with this.
void re_tui_compose(re_tui_t *t, re_panel_t *panels, size_t n);

// A table inside a box. Columns are measured first so alignment holds, then rows are
// written with the heading styled and separators between groups.
typedef struct {
    size_t w[RE_TUI_MAX_COLS];
    size_t n;
    bool inside;
} re_tui_table_t;

void re_tui_table_init(re_tui_table_t *tt, re_tui_t *t);
// A table line is built in a caller supplied buffer rather than a local one, because
// a local growable buffer needs an arena and a null arena is a crash the moment the
// first cell is appended. scratch is cleared first and only needs to outlive the call.
void re_tui_table_head(re_tui_table_t *tt, re_tui_t *t, re_panel_t *p, re_strbuf_t *scratch,
                       const char *const *cols, size_t n);
void re_tui_table_row(re_tui_table_t *tt, re_tui_t *t, re_panel_t *p, re_strbuf_t *scratch,
                      const char *const *cells, size_t n);
#ifdef __cplusplus
}
#endif
