// breakpoint_me.c - tiny inferior with an exported BP target and a sleep loop.
// Module: test (C11).
// Owns: a durable process for attach and a named symbol for software breakpoints.
// Depends: libc only. Standalone debuggee; not linked into antinomia.
#if !defined(_WIN32)
#    define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#    include <windows.h>
#else
#    include <time.h>
#endif

#if defined(_WIN32)
#    define RE_FIXTURE_EXPORT __declspec(dllexport)
#    define RE_FIXTURE_NOINLINE __declspec(noinline)
#else
#    define RE_FIXTURE_EXPORT __attribute__((visibility("default")))
#    define RE_FIXTURE_NOINLINE __attribute__((noinline, used))
#endif

volatile int g_breakpoint_hits;

/* noinline: keep the software-BP symbol as a real call under Release/LTO. */
RE_FIXTURE_EXPORT RE_FIXTURE_NOINLINE void breakpoint_me(void) {
    g_breakpoint_hits++;
}

static void fixture_sleep_ms(unsigned ms) {
#if defined(_WIN32)
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    (void)nanosleep(&ts, NULL);
#endif
}

static unsigned long parse_iters(int argc, char **argv) {
    unsigned long n;
    char *end;

    if (argc < 2) {
        return 0;
    }
    n = strtoul(argv[1], &end, 10);
    if (end == argv[1] || *end != '\0') {
        return 0;
    }
    return n;
}

int main(int argc, char **argv) {
    unsigned long max = parse_iters(argc, argv);
    unsigned long i;

    /* max == 0 means run forever so a debugger can attach. */
    for (i = 0; max == 0 || i < max; i++) {
        breakpoint_me();
        fixture_sleep_ms(50);
    }
    printf("breakpoint_me hits=%d\n", g_breakpoint_hits);
    return 0;
}
