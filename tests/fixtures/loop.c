// loop.c - tiny inferior for launch and single-step smoke tests.
// Module: test (C11).
// Owns: a short-lived loop with a named tick symbol for step targets.
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

volatile unsigned long g_ticks;

/* noinline: keep the step/BP tick symbol callable under Release/LTO. */
RE_FIXTURE_EXPORT RE_FIXTURE_NOINLINE void tick(void) {
    g_ticks++;
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
        return 8;
    }
    n = strtoul(argv[1], &end, 10);
    if (end == argv[1] || *end != '\0') {
        return 8;
    }
    return n;
}

int main(int argc, char **argv) {
    unsigned long max = parse_iters(argc, argv);
    unsigned long i;

    for (i = 0; i < max; i++) {
        tick();
        fixture_sleep_ms(10);
    }
    printf("loop ticks=%lu\n", (unsigned long)g_ticks);
    return 0;
}
