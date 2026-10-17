// edu_crash.c - educational ELF/PE: hit a marker, then intentional null deref.
// Module: test/edu (C11).
// Owns: a deliberate segfault inferior for exception/event debugger demos.
// Depends: libc only. Standalone debuggee; not malware; labeled educational.
#include <stdio.h>

#if defined(_WIN32)
#    define RE_FIXTURE_EXPORT __declspec(dllexport)
#    define RE_FIXTURE_NOINLINE __declspec(noinline)
#else
#    define RE_FIXTURE_EXPORT __attribute__((visibility("default")))
#    define RE_FIXTURE_NOINLINE __attribute__((noinline, used))
#endif

volatile int g_edu_crash_armed;

/* noinline: keep BP/fault targets as real call sites under Release/LTO. */
RE_FIXTURE_EXPORT RE_FIXTURE_NOINLINE void edu_before_crash(void) {
    g_edu_crash_armed = 1;
}

RE_FIXTURE_EXPORT RE_FIXTURE_NOINLINE void edu_crash_now(void) {
    volatile int *p = (volatile int *)0;
    /* Intentional SIGSEGV / ACCESS_VIOLATION for debugger education only. */
    *p = 0xbad;
}

int main(void) {
    printf("edu_crash: educational intentional fault (MIT, safe demo)\n");
    fflush(stdout);
    edu_before_crash();
    edu_crash_now();
    printf("edu_crash: unreachable\n");
    return 0;
}
