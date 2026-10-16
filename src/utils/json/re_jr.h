// re_jr.h - a minimal JSON reader, for the requests that arrive over a socket.
// Module: util (C11).
// Owns: turning bytes into a small read only document. No writing, no DOM editing.
// Depends: re_arena, re_str, re_strbuf.
//
// Why a second JSON file when re_json writes one: the writer streams straight to a
// buffer and never needs to look at a value twice, while a protocol server has to
// find a member by name in a request it did not shape. Those are different problems,
// and folding the reader into the writer would give one type that is good at neither.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "utils/mem/re_arena.h"
#include "utils/text/re_str.h"
#include "utils/text/re_strbuf.h"

typedef enum {
    RE_JR_NULL = 0,
    RE_JR_BOOL,
    RE_JR_NUM,
    RE_JR_STR,
    RE_JR_ARR,
    RE_JR_OBJ,
    RE_JR_BAD, // a parse error, and the whole document is unusable
} re_jr_kind_t;

#define RE_JR_MAX_MEMBERS 64 // members of one object, or items of one array

typedef struct re_jr re_jr_t;

struct re_jr {
    re_jr_kind_t kind;
    // A number is kept as text as well as a double, because an address can be a 64 bit
    // integer and a double cannot represent every one of those exactly. Converting an
    // address to a double and back would quietly corrupt the low bits.
    double num;
    re_str_t raw;
    re_str_t str;   // RE_JR_STR
    bool boolean;   // RE_JR_BOOL
    re_jr_t *items; // RE_ARR and RE_OBJ, arena backed
    re_str_t *keys; // RE_OBJ only, parallel to items
    size_t count;   // items or members
};

// Parse one complete JSON value. Returns false on anything malformed, including
// trailing content after the value: a protocol frame carries exactly one message, and
// extra bytes mean the stream is out of sync, which is worth refusing rather than
// guessing at.
bool re_jr_parse(re_arena_t *a, const char *src, size_t n, re_jr_t *out);

// A member of an object, or NULL when absent or when this is not an object. A missing
// member and a null member are different answers and are told apart by re_jr_has.
const re_jr_t *re_jr_get(const re_jr_t *obj, const char *key);
bool re_jr_has(const re_jr_t *obj, const char *key);

// Convenience accessors that fall back rather than fail, because a request is mostly
// optional fields and a missing string should not be an error at the call site.
re_str_t re_jr_str(const re_jr_t *v, const char *fallback);
int64_t re_jr_i64(const re_jr_t *v, int64_t fallback);
bool re_jr_bool(const re_jr_t *v, bool fallback);

// Write a JSON string, quotes and all, with the escapes JSON requires. This is the
// counterpart to the writer and has to agree with it, which is why it lives here
// rather than being open coded at each call site.
void re_jr_escape(re_strbuf_t *out, re_str_t s);
#ifdef __cplusplus
}
#endif
