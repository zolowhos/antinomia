// re_err.c - error strings and the two record setters, kept free of dependencies.
// Module: util (C11).
// Owns: the code to text table and formatting of a message into re_err_t.msg.
// Depends: re_err.h only. No I/O, no allocation, never truncates silently.
#include "utils/sys/re_err.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

const char *re_err_str(re_err_code_t code) {
    switch (code) {
        case RE_OK:
            return "ok";
        case RE_E_USAGE:
            return "usage error";
        case RE_E_NOTBIN:
            return "not a recognized binary";
        case RE_E_MALFORMED:
            return "malformed binary";
        case RE_E_UNSUPPORTED:
            return "unsupported format or architecture";
        case RE_E_IO:
            return "input or output error";
        case RE_E_NOMEM:
            return "out of memory";
        case RE_E_RANGE:
            return "read out of bounds";
        case RE_E_NOTFOUND:
            return "not found";
        default:
            return "internal error";
    }
}

void re_err_set(re_err_t *e, re_err_code_t code, const char *file, int line, const char *msg) {
    if (!e)
        return;
    e->code = code;
    e->file = file;
    e->line = line;
    if (msg)
        snprintf(e->msg, sizeof(e->msg), "%s", msg);
    else
        e->msg[0] = '\0';
}

void re_err_setf(re_err_t *e, re_err_code_t code, const char *file, int line, const char *fmt,
                 ...) {
    if (!e)
        return;
    e->code = code;
    e->file = file;
    e->line = line;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->msg, sizeof(e->msg), fmt, ap);
    va_end(ap);
}
