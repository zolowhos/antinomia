// re_term.c - the only file that talks to the console.
// Module: cli (C11).
// Owns: re_term.h. Everything platform specific in the front end lives here.
// Depends: re_term.h, re_input.h, re_tui (size and tty policy).
#include "cli/screen/re_term.h"

#include "utils/tui/re_tui.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#    include <windows.h>
#    define RE_READ_HANDLE() GetStdHandle(STD_INPUT_HANDLE)
#else
#    include <errno.h>
#    include <poll.h>
#    include <termios.h>
#    include <unistd.h>
#endif

#define SEQ_ALT_ON "\x1b[?1049h"
#define SEQ_ALT_OFF "\x1b[?1049l"
#define SEQ_CUR_HIDE "\x1b[?25l"
#define SEQ_CUR_SHOW "\x1b[?25h"
#define SEQ_CLEAR "\x1b[2J"
#define SEQ_MOUSE_ON "\x1b[?1000h\x1b[?1003h\x1b[?1006h"
#define SEQ_MOUSE_OFF "\x1b[?1006l\x1b[?1003l\x1b[?1000l"

static void emit(const char *s) {
    if (s)
        fputs(s, stdout);
}

static bool raw_on(re_term_t *t) {
#if defined(_WIN32)
    HANDLE h = RE_READ_HANDLE();
    DWORD mode = 0;
    if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode))
        return false;
    t->saved_in = mode;
    DWORD want = mode;
    // Line input and echo go, or a key arrives only after Enter. Processed input stays,
    // so Ctrl-C still interrupts. Quick edit swallows clicks unless extended flags and
    // mouse input are set in the same call.
    want &= ~(DWORD)(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_QUICK_EDIT_MODE);
    want |= ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS;
    // An old console refuses virtual-terminal input. Degrading beats printing escapes.
    if (!SetConsoleMode(h, want))
        return false;
    t->have_saved = true;
    return true;
#else
    struct termios orig;
    if (tcgetattr(STDIN_FILENO, &orig) != 0)
        return false;
    struct termios raw = orig;
    // ISIG stays, so Ctrl-C still interrupts. The rest is raw: no echo, no line wait.
    raw.c_lflag &= (tcflag_t) ~(ECHO | ICANON | IEXTEN);
    raw.c_iflag &= (tcflag_t) ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= (tcflag_t) ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
        return false;
    t->have_saved = true;
    return true;
#endif
}

#if defined(_WIN32)
static SHORT font_shrink(SHORT n) {
    SHORT v;
    if (n < 2)
        return n;
    v = (SHORT)((n * 4 + 2) / 5);
    return v > 0 ? v : 1;
}

static CONSOLE_FONT_INFOEX font_was;
static int font_mode;

static void font_keys(WORD vk) {
    INPUT in[4];
    int i;
    memset(in, 0, sizeof(in));
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = VK_CONTROL;
    in[1].type = INPUT_KEYBOARD;
    in[1].ki.wVk = vk;
    in[2] = in[1];
    in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3] = in[0];
    in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    // Two steps: a 10pt face lands on 8pt, which is 1.25 times smaller.
    for (i = 0; i < 2; i++)
        SendInput(4, in, sizeof(INPUT));
}

static void font_set(bool smaller) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_FONT_INFOEX f;
    if (!smaller) {
        if (!font_mode)
            return;
        if (font_was.cbSize && h != INVALID_HANDLE_VALUE)
            SetCurrentConsoleFontEx(h, FALSE, &font_was);
        font_keys(VK_ADD);
        font_mode = 0;
        font_was.cbSize = 0;
        return;
    }
    if (font_mode || h == INVALID_HANDLE_VALUE)
        return;
    memset(&f, 0, sizeof(f));
    f.cbSize = sizeof(f);
    if (GetCurrentConsoleFontEx(h, FALSE, &f)) {
        font_was = f;
        f.dwFontSize.Y = font_shrink(f.dwFontSize.Y);
        f.dwFontSize.X = font_shrink(f.dwFontSize.X);
        SetCurrentConsoleFontEx(h, FALSE, &f);
    }
    font_keys(VK_SUBTRACT);
    font_mode = 1;
}
#else
static void font_set(bool smaller) {
    (void)smaller;
}
#endif

static void out_vt(re_term_t *t, bool on) {
#if defined(_WIN32)
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE)
        return;
    if (!on) {
        if (t->have_out)
            SetConsoleMode(h, t->saved_out);
        t->have_out = false;
        return;
    }
    DWORD mode = 0;
    if (!GetConsoleMode(h, &mode))
        return;
    t->saved_out = mode;
    DWORD want = mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (SetConsoleMode(h, want))
        t->have_out = true;
#else
    (void)t;
    (void)on;
#endif
}

bool re_term_open(re_term_t *t, bool want_mouse) {
    re_input_init(&t->in);
    t->open = false;
    t->mouse = false;
    t->alt_screen = false;
    t->degraded = false;
    t->have_saved = false;
    t->have_out = false;
    t->have_cell = false;
    t->mouse_down = 0;
    t->cell_row = 0;
    t->cell_col = 0;
    t->saved_in = 0;
    t->saved_out = 0;
    if (!re_tui_is_tty() || !re_tui_stdin_tty() || !raw_on(t)) {
        t->degraded = true;
        return false;
    }
    out_vt(t, true);
    emit(SEQ_ALT_ON);
    emit(SEQ_CUR_HIDE);
    emit(SEQ_CLEAR);
    t->alt_screen = true;
    t->want_mouse = want_mouse;
    if (want_mouse) {
        emit(SEQ_MOUSE_ON);
        t->mouse = true;
    }
    fflush(stdout);
    t->open = true;
    font_set(true);
    return true;
}

void re_term_finish(re_term_t *t) {
    if (t->mouse) {
        emit(SEQ_MOUSE_OFF);
        t->mouse = false;
    }
    if (t->alt_screen) {
        emit(SEQ_CUR_SHOW);
        emit(SEQ_ALT_OFF);
        t->alt_screen = false;
    }
    fflush(stdout);
}

static void raw_off(re_term_t *t) {
    out_vt(t, false);
    if (!t->have_saved)
        return;
#if defined(_WIN32)
    HANDLE h = RE_READ_HANDLE();
    if (h != INVALID_HANDLE_VALUE)
        SetConsoleMode(h, t->saved_in);
#else
    struct termios cur;
    if (tcgetattr(STDIN_FILENO, &cur) == 0) {
        cur.c_lflag |= (tcflag_t)(ECHO | ICANON | IEXTEN);
        cur.c_iflag |= (tcflag_t)(IXON | ICRNL);
        cur.c_oflag |= (tcflag_t)OPOST;
        cur.c_cc[VMIN] = 1;
        cur.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &cur);
    }
#endif
    t->have_saved = false;
}

void re_term_close(re_term_t *t) {
    font_set(false);
    re_term_finish(t);
    raw_off(t);
    t->open = false;
}

void re_term_pause(re_term_t *t) {
    re_term_finish(t);
    raw_off(t);
    t->open = false;
}

bool re_term_resume(re_term_t *t) {
    if (!raw_on(t))
        return false;
    out_vt(t, true);
    emit(SEQ_ALT_ON);
    emit(SEQ_CUR_HIDE);
    emit(SEQ_CLEAR);
    t->alt_screen = true;
    t->open = true;
    if (t->want_mouse) {
        emit(SEQ_MOUSE_ON);
        t->mouse = true;
    }
    fflush(stdout);
    return true;
}

void re_term_size(uint16_t *rows, uint16_t *cols) {
    re_tui_term_size(rows, cols);
}

bool re_term_has_mouse(const re_term_t *t) {
    return t->mouse;
}

bool re_term_pending(re_term_t *t) {
    if (!t->open)
        return false;
#if defined(_WIN32)
    HANDLE h = RE_READ_HANDLE();
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD ev = 0;
    if (!GetNumberOfConsoleInputEvents(h, &ev) || ev == 0)
        return false;
    return true;
#else
    struct pollfd p;
    p.fd = STDIN_FILENO;
    p.events = POLLIN;
    p.revents = 0;
    return poll(&p, 1, 0) > 0 && (p.revents & POLLIN) != 0;
#endif
}

#if defined(_WIN32)
static void mouse_cell(COORD pos, uint16_t *row, uint16_t *col) {
    int x = pos.X;
    int y = pos.Y;
    HANDLE oh = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (oh != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(oh, &info)) {
        x -= info.srWindow.Left;
        y -= info.srWindow.Top;
    }
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    *col = (uint16_t)x;
    *row = (uint16_t)y;
}

static bool emit_move(re_term_t *t, COORD pos, re_ev_t *out) {
    uint16_t row = 0;
    uint16_t col = 0;
    mouse_cell(pos, &row, &col);
    bool same = t->have_cell && row == t->cell_row && col == t->cell_col;
    t->have_cell = true;
    t->cell_row = row;
    t->cell_col = col;
    if (same)
        return false;
    memset(out, 0, sizeof(*out));
    out->kind = RE_EV_MOUSE;
    out->button = 32;
    out->press = false;
    out->row = row;
    out->col = col;
    return true;
}

static bool take_mouse(re_term_t *t, const MOUSE_EVENT_RECORD *m, re_ev_t *out) {
    uint32_t mask =
        FROM_LEFT_1ST_BUTTON_PRESSED | FROM_LEFT_2ND_BUTTON_PRESSED | RIGHTMOST_BUTTON_PRESSED;
    if (m->dwEventFlags & MOUSE_MOVED) {
        t->mouse_down = m->dwButtonState & mask;
        return emit_move(t, m->dwMousePosition, out);
    }
    memset(out, 0, sizeof(*out));
    out->kind = RE_EV_MOUSE;
    mouse_cell(m->dwMousePosition, &out->row, &out->col);
    if (m->dwEventFlags & MOUSE_WHEELED) {
        short delta = (short)HIWORD(m->dwButtonState);
        out->button = delta > 0 ? (uint8_t)RE_MOUSE_WHEEL_UP : (uint8_t)RE_MOUSE_WHEEL_DOWN;
        out->press = true;
        return true;
    }
    t->have_cell = true;
    t->cell_row = out->row;
    t->cell_col = out->col;
    uint32_t now = m->dwButtonState & mask;
    uint32_t down = now & ~t->mouse_down;
    uint32_t up = t->mouse_down & ~now;
    t->mouse_down = now;
    uint32_t bit = down ? down : up;
    if (!bit)
        return false;
    out->press = down != 0;
    if (bit & FROM_LEFT_1ST_BUTTON_PRESSED)
        out->button = (uint8_t)RE_MOUSE_LEFT;
    else if (bit & FROM_LEFT_2ND_BUTTON_PRESSED)
        out->button = (uint8_t)RE_MOUSE_MIDDLE;
    else
        out->button = (uint8_t)RE_MOUSE_RIGHT;
    return true;
}
#endif

static size_t drain(re_term_t *t) {
    uint8_t buf[128];
    size_t total = 0;
#if defined(_WIN32)
    HANDLE h = RE_READ_HANDLE();
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    DWORD avail = 0;
    while (GetNumberOfConsoleInputEvents(h, &avail) && avail > 0) {
        INPUT_RECORD rec;
        DWORD got = 0;
        if (!ReadConsoleInputA(h, &rec, 1, &got) || got == 0)
            break;
        if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown)
            continue;
        char ch = rec.Event.KeyEvent.uChar.AsciiChar;
        if (!ch)
            continue; // a key with no ASCII byte: a dead key, or a compose in progress
        buf[total % sizeof(buf)] = (uint8_t)ch;
        total++;
        if (total >= sizeof(buf))
            break;
    }
#else
    for (;;) {
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n > 0) {
            total = (size_t)n;
            break;
        }
        if (n < 0 && errno != EAGAIN && errno != EINTR)
            break;
        if (n == 0)
            break;
        break;
    }
#endif
    if (total)
        re_input_feed(&t->in, buf, total);
    return total;
}

bool re_term_wait(re_term_t *t, re_ev_t *out, unsigned timeout_ms) {
    if (!t->open)
        return false;
    for (;;) {
        if (re_input_next(&t->in, out))
            return true;
#if defined(_WIN32)
        // A resize arrives as a console event rather than as bytes, so the window
        // record is drained here instead of waiting for a key that will never come.
        HANDLE h = RE_READ_HANDLE();
        DWORD ev = 0;
        while (h != INVALID_HANDLE_VALUE && GetNumberOfConsoleInputEvents(h, &ev) && ev > 0) {
            INPUT_RECORD rec;
            DWORD got = 0;
            if (!ReadConsoleInputA(h, &rec, 1, &got) || got == 0)
                break;
            if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
                drain(t);
                while (re_input_next(&t->in, out)) {
                }
                out->kind = RE_EV_RESIZE;
                out->key = RE_KEY_RESIZE;
                return true;
            }
            if (rec.EventType == MOUSE_EVENT && take_mouse(t, &rec.Event.MouseEvent, out))
                return true;
            if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
                char ch = rec.Event.KeyEvent.uChar.AsciiChar;
                if (ch)
                    re_input_feed(&t->in, (const uint8_t *)&ch, 1);
            }
        }
        // A character mouse report is complete now. Waiting first drops the click.
        if (re_input_next(&t->in, out))
            return true;
        if (timeout_ms == 0)
            return false;
        WaitForSingleObject(h, timeout_ms);
#else
        struct pollfd p;
        p.fd = STDIN_FILENO;
        p.events = POLLIN;
        p.revents = 0;
        int pr = poll(&p, 1, (int)timeout_ms);
        if (pr <= 0)
            return false; // a timeout, or a signal: the caller redraws either way
        if (drain(t) == 0 && timeout_ms)
            continue; // a wakeup with no bytes yet; try once more within the budget
#endif
        if (re_input_next(&t->in, out))
            return true;
        if (timeout_ms == 0)
            return false;
    }
}

void re_term_write(re_term_t *t, const char *bytes, size_t n) {
    if (!t || !bytes || !n)
        return;
    fwrite(bytes, 1, n, stdout);
    fflush(stdout);
}

// After windows.h. A sorted include block puts this first, before those types exist.
#if defined(_WIN32)
#    include <commdlg.h>
#endif

bool re_term_pick_file(char *out, size_t cap) {
    if (!out || cap < 2)
        return false;
#if defined(_WIN32)
    wchar_t file[1024];
    OPENFILENAMEW ofn;
    // The filter ends in two NULs. One is the literal, one is the string's own.
    static const wchar_t kFilter[] = L"All files\0*.*\0";
    memset(file, 0, sizeof(file));
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetConsoleWindow();
    ofn.lpstrFilter = kFilter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)(sizeof(file) / sizeof(file[0]));
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = L"Open";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn))
        return false;
    if (WideCharToMultiByte(CP_UTF8, 0, file, -1, out, (int)cap, NULL, NULL) <= 1)
        return false;
    return out[0] != '\0';
#else
    return false;
#endif
}
