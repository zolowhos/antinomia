// re_err.h - typed error codes, error record and exit code mapping.
// Module: util (C11).
// Owns: re_err_code_t, re_err_t, message text, exit code mapping, set helpers.
// Depends: none. No I/O, re_log is the only place an error reaches a stream.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    RE_OK = 0,
    RE_E_USAGE,
    RE_E_NOTBIN,
    RE_E_MALFORMED,
    RE_E_UNSUPPORTED,
    RE_E_IO,
    RE_E_NOMEM,
    RE_E_RANGE,
    RE_E_NOTFOUND,
    RE_E_INTERNAL
} re_err_code_t;

#define RE_ERR_MSG_MAX 192

typedef struct {
    re_err_code_t code;
    const char *file;
    int line;
    char msg[RE_ERR_MSG_MAX];
} re_err_t;

const char *re_err_str(re_err_code_t code);

// Process exit code for an error code. Documented in .rules/rules.md section 7.
static inline int re_err_exit_code(re_err_code_t code) {
    switch (code) {
        case RE_OK:
            return 0;
        case RE_E_USAGE:
            return 2;
        case RE_E_NOTBIN:
            return 3;
        case RE_E_MALFORMED:
        case RE_E_RANGE:
            return 4;
        case RE_E_UNSUPPORTED:
            return 5;
        case RE_E_IO:
        case RE_E_NOMEM:
            return 6;
        default:
            return 1;
    }
}

static inline bool re_err_ok(const re_err_t *e) {
    return !e || e->code == RE_OK;
}

static inline const char *re_err_msg(const re_err_t *e) {
    if (!e)
        return "";
    return e->msg[0] ? e->msg : re_err_str(e->code);
}

static inline re_err_code_t re_err_code(const re_err_t *e) {
    return e ? e->code : RE_OK;
}

void re_err_set(re_err_t *e, re_err_code_t code, const char *file, int line, const char *msg);
void re_err_setf(re_err_t *e, re_err_code_t code, const char *file, int line, const char *fmt, ...);

#define RE_ERR_SET(e, code, msg) re_err_set((e), (code), __FILE__, __LINE__, (msg))
#define RE_ERR_SETF(e, code, ...) re_err_setf((e), (code), __FILE__, __LINE__, __VA_ARGS__)
#define RE_ERR_OK(e) ((e)->code = RE_OK)

#ifdef __cplusplus
}
#endif
