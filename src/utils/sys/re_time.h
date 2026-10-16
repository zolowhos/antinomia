// re_time.h - convert a PE timestamp to a readable date. Triage wants driver age.
// Module: util (C11).
// Owns: the civil calendar conversion from days since the epoch, and ISO 8601.
// Depends: stdio only for the clock, which lives here rather than in a util of its
//           own because it is the other half of what this header already owns: time.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

// Milliseconds of processor time since an arbitrary origin, for timing one pass
// against another. clock() rather than a wall clock on purpose: a pass that measures
// 40ms of work should not report 4 seconds because the machine was suspended, and
// there is no monotonic wall clock in C11. Zero when the platform has no usable one,
// which the caller reports as a zero rather than as a fast pass.
static inline uint64_t re_clock_ms(void) {
    clock_t c = clock();
    if (c == (clock_t)-1)
        return 0;
    return (uint64_t)(((uint64_t)c * 1000u) / (uint64_t)CLOCKS_PER_SEC);
}

// Zero padded decimal into a caller buffer. Returns the bytes written, so the
// caller can advance its cursor. Local on purpose: a date is all this formats.
static inline int re_time_pad(char *p, int v, int width) {
    char *start = p;
    char tmp[24];
    int n = 0;
    if (v < 0) {
        *p++ = '-';
        width--;
        v = -v;
    }
    do {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v);
    for (int i = n; i < width; i++)
        *p++ = '0';
    for (int i = n - 1; i >= 0; i--)
        *p++ = tmp[i];
    return (int)(p - start);
}

// Howard Hinnant's days-from-civil inverse. Exact for negative timestamps, which
// pre 1970 builds produce, and for the whole int64 range we can encounter.
static inline void re_time_civil(int64_t z, int *year, unsigned *month, unsigned *day) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = (int64_t)yoe + era * 400;
    unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    unsigned mp = (5u * doy + 2u) / 153u;
    unsigned d = doy - (153u * mp + 2u) / 5u + 1u;
    unsigned m = mp < 10u ? mp + 3u : mp - 9u;
    *year = (int)(y + (m <= 2u ? 1 : 0));
    *month = m;
    *day = d;
}

// Split a Unix timestamp into whole days and the seconds within the day, with
// the floor division done properly so negative values do not truncate toward 0.
static inline void re_time_split(int64_t unix_seconds, int64_t *days, int64_t *secs) {
    *days = unix_seconds / 86400;
    *secs = unix_seconds % 86400;
    if (*secs < 0) {
        *secs += 86400;
        (*days)--;
    }
}

// Build year alone. Driver age is the single most useful triage fact, because
// unpatched old drivers are the ones worth escalating.
static inline int re_time_year(int64_t unix_seconds) {
    int64_t days;
    int64_t secs;
    re_time_split(unix_seconds, &days, &secs);
    int y;
    unsigned m;
    unsigned d;
    re_time_civil(days, &y, &m, &d);
    return y;
}

static inline int re_time_age_years(int64_t unix_seconds, int64_t now_unix) {
    return re_time_year(now_unix) - re_time_year(unix_seconds);
}

// ISO 8601 in UTC, "YYYY-MM-DDTHH:MM:SSZ". buf must hold at least 21 bytes.
static inline void re_time_iso(int64_t unix_seconds, char *buf, size_t cap) {
    if (cap < 21) {
        if (cap)
            buf[0] = '\0';
        return;
    }
    int64_t days;
    int64_t secs;
    re_time_split(unix_seconds, &days, &secs);
    int y;
    unsigned m;
    unsigned d;
    re_time_civil(days, &y, &m, &d);
    char *p = buf;
    p += re_time_pad(p, y, 4);
    *p++ = '-';
    p += re_time_pad(p, (int)m, 2);
    *p++ = '-';
    p += re_time_pad(p, (int)d, 2);
    *p++ = 'T';
    p += re_time_pad(p, (int)(secs / 3600), 2);
    *p++ = ':';
    p += re_time_pad(p, (int)((secs % 3600) / 60), 2);
    *p++ = ':';
    p += re_time_pad(p, (int)(secs % 60), 2);
    *p++ = 'Z';
    *p = '\0';
}
