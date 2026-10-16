// re_input.c - the decoder state machine and the key names.
// Module: cli (C11).
// Owns: re_input.h's decoder. No input is read here and no terminal is touched.
// Depends: re_input.h only. Every branch is reachable from a byte string in a test,
//           which is the point of keeping this apart from the platform layer.
#include "cli/screen/re_input.h"

void re_input_init(re_input_t *in) {
    in->n = 0;
    in->sticky_escape = false;
}

void re_input_feed(re_input_t *in, const uint8_t *p, size_t n) {
    if (!p)
        return;
    for (size_t i = 0; i < n; i++) {
        if (in->n == RE_INPUT_MAX) {
            // Full. Drop the oldest byte rather than the newest: the newest is the one
            // a person just pressed, and an input stream that is behind is already
            // losing keystrokes.
            for (size_t k = 1; k < in->n; k++)
                in->buf[k - 1] = in->buf[k];
            in->n--;
        }
        in->buf[in->n++] = p[i];
    }
}

// How many bytes the sequence starting at the first byte takes, or 0 when it is not
// yet complete. The three terminators are the whole story: a final byte, a CSI, or an
// SS3. Anything longer than that is not a key.
static size_t seq_len(const uint8_t *b, size_t n) {
    if (n < 2)
        return 0;
    if (b[1] != '[' && b[1] != 'O')
        return 0; // not a sequence at all: ESC plus a plain byte is Alt
    if (n < 3)
        return 0;
    // A CSI parameter or intermediate section runs until a byte in 0x40..0x7e.
    for (size_t i = 2; i < n; i++) {
        uint8_t c = b[i];
        if (c >= 0x40 && c <= 0x7e)
            return i + 1;
        if (c == 0x1b) // a new escape means the old one never finished
            return 0;
        if (i > RE_INPUT_MAX - 2u)
            return 0;
    }
    return 0;
}

// The parameters between the introducer and the final byte. False when there are more
// of them than anything here models, which is how an unrecognised sequence is told
// apart from a malformed one.
static bool parse_params(const uint8_t *b, size_t len, uint32_t *params, size_t *count) {
    size_t np = 0;
    // An SGR mouse puts a '<' where a parameter would begin, so the
    // parameter run starts after it rather than at it.
    size_t i0 = (b[2] == '<') ? 3u : 2u;
    for (size_t i = i0; i + 1 < len; i++) {
        uint8_t c = b[i];
        if (c >= '0' && c <= '9') {
            if (np >= 4)
                return false;
            params[np] = params[np] * 10u + (uint32_t)(c - '0');
        } else if (c == ';') {
            if (np + 1 >= 4)
                return false;
            np++;
        } else {
            return false;
        }
    }
    *count = np + 1;
    return true;
}

// The SGR mouse form: an introducer of '<', a button that may carry the wheel bits, and
// one based coordinates. The one based to zero based conversion happens here so that
// nothing downstream has to remember which convention this protocol used, and a hit
// test off by one is the kind of bug that looks like a layout problem.
static bool decode_mouse(const uint8_t *b, size_t len, const uint32_t *p, size_t count,
                         re_ev_t *out) {
    uint8_t fin = b[len - 1];
    if (b[2] != '<' || (fin != 'M' && fin != 'm') || count < 3)
        return false;
    out->kind = RE_EV_MOUSE;
    out->button = (uint8_t)p[0];
    out->col = (uint16_t)(p[1] ? p[1] - 1u : 0u);
    out->row = (uint16_t)(p[2] ? p[2] - 1u : 0u);
    out->press = fin == 'M';
    return true;
}

// The numeric forms, where the final is '~' and the first parameter is the key. The
// function key numbers are not contiguous in the protocol, so each group is mapped onto
// the contiguous range the key names use. False for a number this table does not know,
// which is the honest answer for a terminal with more keys than the view has commands.
static bool decode_tilde(uint32_t p0, re_ev_t *out) {
    switch (p0) {
        case 1:
        case 7:
            out->key = RE_KEY_HOME;
            break;
        case 2:
            out->key = RE_KEY_INSERT;
            break;
        case 3:
            out->key = RE_KEY_DELETE;
            break;
        case 4:
        case 8:
            out->key = RE_KEY_END;
            break;
        case 5:
            out->key = RE_KEY_PGUP;
            break;
        case 6:
            out->key = RE_KEY_PGDN;
            break;
        case 11:
        case 15:
            out->key = RE_KEY_F1;
            break;
        case 17:
        case 18:
        case 19:
        case 20:
        case 21:
            out->key = RE_KEY_F1 + (p0 - 10u);
            break;
        case 23:
        case 24:
            out->key = RE_KEY_F12;
            break;
        default:
            return false;
    }
    return true;
}

// The single letter finals: the arrows, Home, End and shift-tab.
static bool decode_letter(uint8_t fin, re_ev_t *out) {
    switch (fin) {
        case 'A':
            out->key = RE_KEY_UP;
            break;
        case 'B':
            out->key = RE_KEY_DOWN;
            break;
        case 'C':
            out->key = RE_KEY_RIGHT;
            break;
        case 'D':
            out->key = RE_KEY_LEFT;
            break;
        case 'H':
            out->key = RE_KEY_HOME;
            break;
        case 'F':
            out->key = RE_KEY_END;
            break;
        case 'Z':
            out->key = RE_KEY_STAB;
            break;
        default:
            return false;
    }
    return true;
}

// Decode a complete CSI or SS3 sequence. Anything this does not model is refused, and
// the bytes are dropped: a made up key is worse than a dropped one, because a dropped
// one does nothing and a made up one does something to the state.
static bool decode_seq(const uint8_t *b, size_t len, re_ev_t *out) {
    uint32_t params[4] = {0, 0, 0, 0};
    size_t count = 0;
    if (!parse_params(b, len, params, &count))
        return false;
    if (decode_mouse(b, len, params, count, out))
        return true;
    // The two key forms produce a code but not an event: the kind is set here so that
    // a decoder which fills in the code and forgets the kind produces nothing at all,
    // rather than an event with no kind that a caller will act on.
    if (decode_letter(b[len - 1], out)) {
        out->kind = RE_EV_KEY;
        return true;
    }
    if (b[len - 1] != '~')
        return false;
    if (!decode_tilde(params[0], out))
        return false;
    out->kind = RE_EV_KEY;
    return true;
}

// A UTF-8 character. Its length comes from the leading byte and the decoder waits for
// all of it, so a character split across two reads is one key rather than two.
static size_t decode_utf8(const uint8_t *b, size_t n, re_ev_t *out) {
    uint8_t c = b[0];
    size_t need = 1;
    if (c >= 0xf0)
        need = 4;
    else if (c >= 0xe0)
        need = 3;
    else if (c >= 0xc0)
        need = 2;
    if (n < need)
        return 0;
    // One byte is the codepoint itself. Anything longer masks off its own leading
    // marker bits first, which is what folds a four byte sequence into one plane.
    uint32_t cp = c;
    if (need == 2)
        cp = (uint32_t)(c & 0x1fu);
    else if (need == 3)
        cp = (uint32_t)(c & 0x0fu);
    else if (need == 4)
        cp = (uint32_t)(c & 0x07u);
    for (size_t i = 1; i < need; i++) {
        if ((b[i] & 0xc0) != 0x80)
            return 1; // malformed: consume one and let the rest decode on its own
        cp = (cp << 6) | (uint32_t)(b[i] & 0x3fu);
    }
    out->kind = RE_EV_KEY;
    out->key = cp;
    out->ch = cp;
    return need;
}

// ESC, then a byte. If that byte can continue a sequence the sequence is decoded;
// otherwise the two bytes are read as Alt and that key. A bare Escape and Alt-Escape
// are indistinguishable without a timing rule this decoder does not have, and Alt is
// assumed because a bare Escape still leaves the view when it is the same number.
static size_t decode_escape(const uint8_t *b, size_t n, re_ev_t *out) {
    uint8_t nxt = b[1];
    if (nxt == '[' || nxt == 'O') {
        size_t len = seq_len(b, n);
        if (!len)
            return 0;
        if (!decode_seq(b, len, out))
            return len; // consumed and discarded, deliberately
        return len;
    }
    out->kind = RE_EV_KEY;
    out->alt = true;
    out->key = nxt;
    out->ch = nxt;
    return 2;
}

// A plain byte. Returns the number of bytes consumed, or 0 when the buffer does not
// hold a whole character yet.
static size_t decode_one(const uint8_t *b, size_t n, re_ev_t *out) {
    uint8_t c = b[0];
    if (c == 0x1b)
        return decode_escape(b, n, out);
    if (c == 0x0d || c == 0x0a) {
        out->kind = RE_EV_KEY;
        out->key = RE_KEY_ENTER;
        return 1;
    }
    if (c == 0x09) {
        out->kind = RE_EV_KEY;
        out->key = RE_KEY_TAB;
        return 1;
    }
    if (c == 0x7f || c == 0x08) {
        out->kind = RE_EV_KEY;
        out->key = RE_KEY_BACKSPACE;
        return 1;
    }
    if (c < 0x20) {
        out->kind = RE_EV_KEY;
        // Control is the letter minus a quarter, which is what makes Ctrl-C the number
        // 3. ^@ needs its own case: it is Ctrl-Space rather than Ctrl-At.
        out->ctrl = true;
        out->key = c;
        out->ch = c;
        return 1;
    }
    return decode_utf8(b, n, out);
}

bool re_input_next(re_input_t *in, re_ev_t *out) {
    for (;;) {
        if (!in->n)
            return false;
        // Reset first: a sequence that is recognised as bytes but modelled as no key
        // has to leave the kind at none, or the caller sees last run's event.
        *out = (re_ev_t){0};
        size_t used = decode_one(in->buf, in->n, out);
        if (!used)
            return false; // incomplete: leave every byte in place
        for (size_t i = used; i < in->n; i++)
            in->buf[i - used] = in->buf[i];
        in->n -= used;
        if (out->kind != RE_EV_NONE)
            return true;
        // Consumed, no event, and something may be decodable behind it.
    }
}

bool re_input_is_click(const re_ev_t *ev) {
    if (!ev || ev->kind != RE_EV_MOUSE || !ev->press)
        return false;
    // Bit 5 is a move and bit 6 is the wheel. Both set press in the report, and
    // treating either as a click would load a file when the reader scrolled.
    if ((ev->button & 96u) != 0)
        return false;
    return (ev->button & 3u) == RE_MOUSE_LEFT;
}

const char *re_input_key_name(uint32_t key) {
    switch (key) {
        case RE_KEY_UP:
            return "Up";
        case RE_KEY_DOWN:
            return "Down";
        case RE_KEY_LEFT:
            return "Left";
        case RE_KEY_RIGHT:
            return "Right";
        case RE_KEY_HOME:
            return "Home";
        case RE_KEY_END:
            return "End";
        case RE_KEY_PGUP:
            return "PgUp";
        case RE_KEY_PGDN:
            return "PgDn";
        case RE_KEY_INSERT:
            return "Ins";
        case RE_KEY_DELETE:
            return "Del";
        case RE_KEY_ENTER:
            return "Enter";
        case RE_KEY_TAB:
            return "Tab";
        case RE_KEY_STAB:
            return "S-Tab";
        case RE_KEY_ESCAPE:
            return "Esc";
        case RE_KEY_BACKSPACE:
            return "Bksp";
        case RE_KEY_RESIZE:
            return "Resize";
        default:
            break;
    }
    if (key >= RE_KEY_F1 && key <= RE_KEY_F12) {
        // Static names rather than a formatted buffer, so a caller can hold the
        // pointer and a status bar does not allocate.
        static const char *kNames[12] = {"F1", "F2", "F3", "F4",  "F5",  "F6",
                                         "F7", "F8", "F9", "F10", "F11", "F12"};
        return kNames[key - RE_KEY_F1];
    }
    if (key < 0x20 || key == 0x7f)
        return "Ctrl";
    if (key >= 0x20 && key < 0x7f)
        return "Char";
    return "?";
}
