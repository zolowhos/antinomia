// re_term.h - the terminal session: raw input, the alternate screen, mouse reporting.
// Module: cli (C11).
// Owns: everything that talks to the OS console, and nothing that decides anything.
// Depends: re_input.h for the decoder it feeds. All platform code is behind this
//           header, so the rest of the front end builds and tests without it.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cli/screen/re_input.h"

typedef struct {
    bool open;
    bool mouse;      // mouse reporting was turned on and is believed to be on
    bool alt_screen; // the alternate screen was entered
    bool degraded;   // no terminal to talk to, so this is a no-op session
    bool want_mouse; // mouse reporting was asked for, whether or not it took
    re_input_t in;
    uint32_t saved_in; // console mode, Windows only
    uint32_t saved_out;
    uint32_t mouse_down; // buttons held, so a move is not reported as another press
    uint16_t cell_row;   // last reported mouse cell, so a move inside it is dropped
    uint16_t cell_col;
    bool have_cell;
    bool have_saved;
    bool have_out; // output mode was changed and must be put back
} re_term_t;

// Enter raw mode, the alternate screen, and optionally mouse reporting. Returns false
// when either end is not a terminal: the caller is expected to say so and print a
// report instead, because a full screen session against a pipe produces bytes nobody
// asked for and no way to leave it.
bool re_term_open(re_term_t *t, bool want_mouse);

// Put the terminal back exactly as it was. Safe to call twice, and safe to call when
// open failed, because the usual way this program ends is somebody pressing Ctrl-C and
// the handler calling straight through to here.
void re_term_close(re_term_t *t);

// The current size. On Windows the console buffer is consulted, elsewhere the tty is,
// so a resize is noticed by asking rather than by being told.
void re_term_size(uint16_t *rows, uint16_t *cols);

// The next event, waiting up to timeout_ms. False on a timeout, which is the normal
// case: the loop wakes on a timer to redraw rather than blocking on a keystroke.
bool re_term_wait(re_term_t *t, re_ev_t *out, unsigned timeout_ms);

// Whether anything is waiting right now, without blocking at all.
bool re_term_pending(re_term_t *t);

// Send bytes to the terminal. Everything the front end draws goes through here, so
// there is one place that knows how output happens.
void re_term_write(re_term_t *t, const char *bytes, size_t n);

// Show the cursor and leave the alternate screen, without turning raw mode off. Used
// on the way out; close does the rest.
void re_term_finish(re_term_t *t);

// Whether the terminal was asked for mouse reporting and the request was made. A
// front end uses this to decide whether to say "click" or to leave it unsaid.
bool re_term_has_mouse(const re_term_t *t);

// Step out of the full screen and back to cooked input, so a line can be typed with
// echo and with the terminal's own editing. This is what a prompt needs: a program that
// asks a question while the cursor is hidden and echo is off is asking it badly.
//
// Pause and resume are a pair, and resume restores mouse reporting too, because the
// pause is usually a question and the answer comes back into the same session.
void re_term_pause(re_term_t *t);
bool re_term_resume(re_term_t *t);

// The system open-file dialog. False on cancel, on a path that does not fit, and
// where the platform has no dialog. The caller stays on the current screen: this
// does not read a typed path.
bool re_term_pick_file(char *out, size_t cap);

#ifdef __cplusplus
}
#endif
