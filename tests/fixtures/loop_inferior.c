// loop_inferior.c - tiny sleeper used by Linux debug integration tests.
// Module: test (C11).
// Owns: long-lived process, export marker, watch cell, optional dlopen.
// Depends: unistd, dlfcn. Built only on Linux for process-control tests.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <unistd.h>

#if defined(__GNUC__)
__attribute__((visibility("default")))
#endif
volatile int antinomia_watch = 0;

/* noinline+used: clang-release LTO otherwise inlines touch into main and
 * software BPs on the exported symbol never fire (phase1 / edu smokes). */
#if defined(__GNUC__)
__attribute__((visibility("default"), noinline, used))
#endif
void antinomia_marker(void) {
}

#if defined(__GNUC__)
__attribute__((visibility("default"), noinline, used))
#endif
void antinomia_touch(void) {
    antinomia_watch = 0x5a;
}

int main(void) {
    antinomia_marker();
    /* Trigger a late module load so bpdll / r_brk tests have something to hit. */
    (void)dlopen("libm.so.6", RTLD_NOW);
    for (;;) {
        antinomia_touch();
        usleep(1000);
    }
    return 0;
}
