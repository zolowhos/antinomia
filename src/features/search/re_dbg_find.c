// re_dbg_find.c - scan inferior memory for byte patterns and typed values.
// Module: feature (C11).
// Owns: pattern parse and chunked search used by find/search CLI+MCP.
// Depends: re_dbg_find.h, re_hex, re_str. No OS headers.
#include "features/search/re_dbg_find.h"

#include "utils/text/re_hex.h"
#include "utils/text/re_str.h"

#include <string.h>

static bool parse_u64(const char *s, uint64_t *out) {
    re_str_t v;
    uint64_t base = 10;
    size_t i = 0;
    uint64_t acc = 0;
    if (!s || !s[0])
        return false;
    v = re_str(s);
    if (v.n > 2 && v.p[0] == '0' && (v.p[1] == 'x' || v.p[1] == 'X')) {
        base = 16;
        i = 2;
    }
    if (i >= v.n)
        return false;
    for (; i < v.n; i++) {
        int d = re_hex_val(v.p[i]);
        if (d < 0 || (uint64_t)d >= base)
            return false;
        acc = acc * base + (uint64_t)d;
    }
    *out = acc;
    return true;
}

static re_err_code_t parse_typed(re_str_t s, re_dbg_find_pat_t *out) {
    size_t width = 0;
    const char *val;
    uint64_t n = 0;
    size_t k;
    if (re_str_istarts_cstr(s, "u8:")) {
        width = 1;
        val = s.p + 3;
    } else if (re_str_istarts_cstr(s, "u16:")) {
        width = 2;
        val = s.p + 4;
    } else if (re_str_istarts_cstr(s, "u32:") || re_str_istarts_cstr(s, "dword:")) {
        width = 4;
        val = s.p + (re_str_istarts_cstr(s, "dword:") ? 6 : 4);
    } else if (re_str_istarts_cstr(s, "u64:") || re_str_istarts_cstr(s, "qword:")) {
        width = 8;
        val = s.p + (re_str_istarts_cstr(s, "qword:") ? 6 : 4);
    } else {
        return RE_E_USAGE;
    }
    if (!parse_u64(val, &n))
        return RE_E_USAGE;
    out->n = width;
    for (k = 0; k < width; k++) {
        out->bytes[k] = (uint8_t)((n >> (8u * k)) & 0xffu);
        out->mask[k] = 0xffu;
    }
    return RE_OK;
}

static re_err_code_t parse_quoted(re_str_t s, re_dbg_find_pat_t *out) {
    size_t i;
    if (s.n < 2 || s.p[0] != '"' || s.p[s.n - 1] != '"')
        return RE_E_USAGE;
    out->n = s.n - 2u;
    if (out->n == 0 || out->n > RE_FIND_PAT_MAX)
        return RE_E_USAGE;
    for (i = 0; i < out->n; i++) {
        out->bytes[i] = (uint8_t)s.p[i + 1u];
        out->mask[i] = 0xffu;
    }
    return RE_OK;
}

static int hex_nibble(char c) {
    if (c == '?')
        return -2;
    return re_hex_val(c);
}

static re_err_code_t parse_hex_pat(re_str_t s, re_dbg_find_pat_t *out) {
    size_t i = 0;
    out->n = 0;
    while (i < s.n) {
        int hi, lo;
        if (s.p[i] == ' ' || s.p[i] == '\t') {
            i++;
            continue;
        }
        if (out->n >= RE_FIND_PAT_MAX)
            return RE_E_USAGE;
        if (i + 1u >= s.n)
            return RE_E_USAGE;
        hi = hex_nibble(s.p[i]);
        lo = hex_nibble(s.p[i + 1u]);
        if (hi == -1 || lo == -1)
            return RE_E_USAGE;
        out->bytes[out->n] = (uint8_t)(((hi < 0 ? 0 : hi) << 4) | (lo < 0 ? 0 : lo));
        out->mask[out->n] = (uint8_t)(((hi < 0 ? 0 : 0xf0) | (lo < 0 ? 0 : 0x0f)));
        out->n++;
        i += 2;
    }
    return out->n ? RE_OK : RE_E_USAGE;
}

re_err_code_t re_dbg_find_parse(const char *spec, re_dbg_find_pat_t *out) {
    re_str_t s;
    if (!spec || !out)
        return RE_E_USAGE;
    memset(out, 0, sizeof(*out));
    s = re_str(spec);
    if (s.n == 0)
        return RE_E_USAGE;
    if (s.p[0] == '"')
        return parse_quoted(s, out);
    if (re_str_istarts_cstr(s, "u8:") || re_str_istarts_cstr(s, "u16:") ||
        re_str_istarts_cstr(s, "u32:") || re_str_istarts_cstr(s, "u64:") ||
        re_str_istarts_cstr(s, "dword:") || re_str_istarts_cstr(s, "qword:"))
        return parse_typed(s, out);
    return parse_hex_pat(s, out);
}

static bool match_at(const uint8_t *hay, const re_dbg_find_pat_t *pat) {
    size_t i;
    for (i = 0; i < pat->n; i++) {
        if (((hay[i] ^ pat->bytes[i]) & pat->mask[i]) != 0)
            return false;
    }
    return true;
}

static void scan_buf(uint64_t base, const uint8_t *buf, size_t n, const re_dbg_find_pat_t *pat,
                     re_dbg_find_hit_t *hits, size_t cap, size_t *n_out, bool *truncated) {
    size_t i;
    if (pat->n == 0 || n < pat->n)
        return;
    for (i = 0; i + pat->n <= n; i++) {
        if (!match_at(buf + i, pat))
            continue;
        if (*n_out >= cap) {
            *truncated = true;
            return;
        }
        hits[(*n_out)++].addr = base + i;
    }
}

static re_err_code_t scan_range(re_dbg_session_t *s, uint64_t start, uint64_t len,
                                const re_dbg_find_pat_t *pat, re_dbg_find_hit_t *hits, size_t cap,
                                size_t *n_out, bool *truncated) {
    uint8_t chunk[RE_FIND_CHUNK];
    uint64_t off = 0;
    size_t overlap = pat->n > 1 ? pat->n - 1u : 0;
    while (off < len && !*truncated) {
        size_t want = RE_FIND_CHUNK;
        size_t got = 0;
        if (want > (size_t)(len - off))
            want = (size_t)(len - off);
        if (re_dbg_session_read_mem(s, start + off, chunk, want) != RE_OK) {
            off += want;
            continue;
        }
        got = want;
        scan_buf(start + off, chunk, got, pat, hits, cap, n_out, truncated);
        if (off + want >= len)
            break;
        if (want <= overlap)
            off += 1;
        else
            off += want - overlap;
    }
    return RE_OK;
}

re_err_code_t re_dbg_session_find(re_dbg_session_t *s, uint64_t start, uint64_t len,
                                  const re_dbg_find_pat_t *pat, re_dbg_find_hit_t *hits,
                                  size_t cap, size_t *n_out, bool *truncated) {
    re_dbg_module_t mods[64];
    size_t nmod = 0, i;
    if (n_out)
        *n_out = 0;
    if (truncated)
        *truncated = false;
    if (!s || !pat || !hits || !n_out || !truncated)
        return RE_E_USAGE;
    if (!s->attached)
        return RE_E_USAGE;
    if (pat->n == 0)
        return RE_E_USAGE;
    if (len != 0)
        return scan_range(s, start, len, pat, hits, cap, n_out, truncated);
    if (re_dbg_session_enum_modules(s, mods, 64, &nmod) != RE_OK || nmod == 0) {
        if (start == 0)
            return RE_E_NOTFOUND;
        return scan_range(s, start, 0x10000ull, pat, hits, cap, n_out, truncated);
    }
    for (i = 0; i < nmod && !*truncated; i++) {
        uint64_t sz = mods[i].size ? mods[i].size : 0x1000ull;
        if (sz > 0x200000ull)
            sz = 0x200000ull;
        (void)scan_range(s, mods[i].base, sz, pat, hits, cap, n_out, truncated);
    }
    return RE_OK;
}
