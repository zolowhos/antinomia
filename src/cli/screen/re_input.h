// re_input.h - turning terminal bytes into events, one byte at a time.
// Module: cli (C11).
// Owns: the decoder and the key codes. Reads no input itself and touches no file.
// Depends: nothing. The one piece of a terminal front end that can be fully tested
//           without a terminal, which is why it is worth having on its own.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Key codes above the byte range are the ones a single byte cannot express. The
// printable range is the codepoint itself, so a key is one number and there is no
// second table to keep in step.
#define RE_KEY_BASE 0x110000u
#define RE_KEY_UP RE_KEY_BASE
#define RE_KEY_DOWN (RE_KEY_BASE + 1u)
#define RE_KEY_LEFT (RE_KEY_BASE + 2u)
#define RE_KEY_RIGHT (RE_KEY_BASE + 3u)
#define RE_KEY_HOME (RE_KEY_BASE + 4u)
#define RE_KEY_END (RE_KEY_BASE + 5u)
#define RE_KEY_PGUP (RE_KEY_BASE + 6u)
#define RE_KEY_PGDN (RE_KEY_BASE + 7u)
#define RE_KEY_INSERT (RE_KEY_BASE + 8u)
#define RE_KEY_DELETE (RE_KEY_BASE + 9u)
#define RE_KEY_ENTER (RE_KEY_BASE + 10u)
#define RE_KEY_TAB (RE_KEY_BASE + 11u)
#define RE_KEY_STAB (RE_KEY_BASE + 12u) // shift-tab
#define RE_KEY_ESCAPE (RE_KEY_BASE + 13u)
#define RE_KEY_BACKSPACE (RE_KEY_BASE + 14u)
#define RE_KEY_F1 (RE_KEY_BASE + 20u) // F1..F12 are contiguous from here
#define RE_KEY_F12 (RE_KEY_BASE + 31u)
#define RE_KEY_RESIZE (RE_KEY_BASE + 40u)
#define RE_KEY_MAX RE_KEY_BASE + 64u

// Mouse buttons, as the SGR report encodes them. The wheel bits are part of the same
// number in the protocol, so they live in the same enum rather than beside it.
#define RE_MOUSE_LEFT 0u
#define RE_MOUSE_MIDDLE 1u
#define RE_MOUSE_RIGHT 2u
#define RE_MOUSE_WHEEL_UP 64u
#define RE_MOUSE_WHEEL_DOWN 65u

typedef enum {
    RE_EV_NONE = 0,
    RE_EV_KEY,
    RE_EV_MOUSE,
    RE_EV_RESIZE,
} re_ev_kind_t;

typedef struct {
    re_ev_kind_t kind;
    uint32_t key; // RE_EV_KEY: a RE_KEY_ code or a printable codepoint
    uint32_t ch;  // the codepoint, also set for keys that have one
    uint16_t row; // RE_EV_MOUSE: zero based, as the protocol reports it
    uint16_t col;
    uint8_t button; // RE_MOUSE_*
    bool press;     // false for a release report
    bool ctrl;
    bool alt;
    bool shift;
} re_ev_t;

// The pending byte run. A terminal hands over whatever arrived, which is routinely
// half an escape sequence, so the decoder keeps the tail and refuses to guess until
// the sequence is complete. Guessing is how a paste turns into a screenful of cursor
// keys.
#define RE_INPUT_MAX 64u

typedef struct {
    uint8_t buf[RE_INPUT_MAX];
    size_t n;
    bool sticky_escape; // a lone ESC has been seen and may yet become a sequence
} re_input_t;

void re_input_init(re_input_t *in);

// Add bytes. Drops the oldest if the buffer is full, because an input stream that
// cannot be consumed fast enough has already lost data and refusing the new bytes
// too would only make the picture worse.
void re_input_feed(re_input_t *in, const uint8_t *p, size_t n);

// The next event, or false when the bytes so far do not make a complete one. Bytes
// are consumed only when an event is produced, so a false return leaves the decoder
// exactly where it was.
bool re_input_next(re_input_t *in, re_ev_t *out);

// A primary-button press. Motion and the wheel share the button byte in the SGR
// report, and neither of them activates a control.
bool re_input_is_click(const re_ev_t *ev);

// A name for a key, for a status bar. "Up" rather than 0x110000, because a status bar
// is read by a person. Unknown keys come back as "?" rather than as a number.
const char *re_input_key_name(uint32_t key);

#ifdef __cplusplus
}
#endif
