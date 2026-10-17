// edu_hello_spin.c - educational ELF/PE: print hello, then spin on a BP marker.
// Module: test/edu (C11).
// Owns: a clear "hello then spin" inferior for launch/BP/step demos (MIT, safe).
// Depends: libc only. Standalone debuggee; not malware; labeled educational.
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

volatile unsigned long g_edu_spins;

/* noinline: Release/LTO must not inline away the software-BP target. */
RE_FIXTURE_EXPORT RE_FIXTURE_NOINLINE void edu_marker(void) {
    g_edu_spins++;
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

    /* Educational dummy: announce, then spin so a debugger can attach / BP. */
    printf("edu_hello_spin: hello (educational fixture, MIT, safe)\n");
    fflush(stdout);

    /* max == 0 means spin forever (debugger attach / headless kill). */
    for (i = 0; max == 0 || i < max; i++) {
        edu_marker();
        fixture_sleep_ms(25);
    }
    printf("edu_hello_spin: spins=%lu\n", (unsigned long)g_edu_spins);
    return 0;
}
