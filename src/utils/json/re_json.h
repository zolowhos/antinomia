// re_json.h - streaming JSON writer. The only thing allowed to write to stdout.
// Module: util (C11).
// Owns: object and array nesting, string escaping, numbers, and the depth guard.
// Depends: re_strbuf and re_str. No DOM, so a 40KB driver emits with no churn.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "utils/text/re_strbuf.h"

#define RE_JSON_MAX_DEPTH 32

typedef struct {
    re_strbuf_t buf;
    re_arena_t *arena;
    int depth;
    bool need_comma[RE_JSON_MAX_DEPTH];
    bool overflow;
} re_jw_t;

void re_jw_init(re_jw_t *w, re_arena_t *a);
void re_jw_obj(re_jw_t *w);
void re_jw_obj_end(re_jw_t *w);
void re_jw_arr(re_jw_t *w);
void re_jw_arr_end(re_jw_t *w);
void re_jw_key(re_jw_t *w, const char *key);
void re_jw_key_re_str(re_jw_t *w, re_str_t key);
void re_jw_str(re_jw_t *w, re_str_t v);
void re_jw_cstr(re_jw_t *w, const char *v);
void re_jw_u64(re_jw_t *w, uint64_t v);
void re_jw_i64(re_jw_t *w, int64_t v);
void re_jw_hex(re_jw_t *w, uint64_t v, int digits);
void re_jw_f64(re_jw_t *w, double v);
void re_jw_bool(re_jw_t *w, bool v);
void re_jw_null(re_jw_t *w);

// Key then value, for the common object member shape.
void re_jw_kstr(re_jw_t *w, const char *key, re_str_t v);
void re_jw_kcstr(re_jw_t *w, const char *key, const char *v);
void re_jw_ku64(re_jw_t *w, const char *key, uint64_t v);
void re_jw_ki64(re_jw_t *w, const char *key, int64_t v);
void re_jw_khex(re_jw_t *w, const char *key, uint64_t v, int digits);
void re_jw_kf64(re_jw_t *w, const char *key, double v);
void re_jw_kbool(re_jw_t *w, const char *key, bool v);
void re_jw_knull(re_jw_t *w, const char *key);

// Emit an array of unsigned 64 bit values, used for offsets and sizes.
void re_jw_ku64_array(re_jw_t *w, const char *key, const uint64_t *v, size_t n);

// Flush to a stream. The one and only stdout writer in the project, which is
// what keeps the mcp stdio stream clean.
void re_jw_flush(re_jw_t *w, void *stream);

// The writer is not reentrant, so nothing else may write stdout while one is
// open. This asserts that at the point of flush rather than trusting a comment.
bool re_jw_ok(const re_jw_t *w);
#ifdef __cplusplus
}
#endif
