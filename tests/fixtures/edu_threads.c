// edu_threads.c - educational ELF/PE: light multi-thread software-BP storm.
// Module: test/edu (C11).
// Owns: a few worker threads calling one exported hit symbol for BP demos.
// Depends: libc + OS threads. Standalone debuggee; not malware; educational.
#if !defined(_WIN32)
#    define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#    include <windows.h>
#else
#    include <pthread.h>
#    include <time.h>
#    include <unistd.h>
#endif

#if defined(_WIN32)
#    define RE_FIXTURE_EXPORT __declspec(dllexport)
#    define RE_FIXTURE_NOINLINE __declspec(noinline)
#else
#    define RE_FIXTURE_EXPORT __attribute__((visibility("default")))
#    define RE_FIXTURE_NOINLINE __attribute__((noinline, used))
#endif

enum { EDU_THREAD_N = 4 };

volatile unsigned long g_edu_hits;

/* noinline: Release/LTO must keep a callable BP-storm target. */
RE_FIXTURE_EXPORT RE_FIXTURE_NOINLINE void edu_worker_hit(void) {
    g_edu_hits++;
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

#if defined(_WIN32)
static DWORD WINAPI worker_main(void *arg) {
    (void)arg;
    for (;;) {
        edu_worker_hit();
        fixture_sleep_ms(15);
    }
    return 0;
}
#else
static void *worker_main(void *arg) {
    (void)arg;
    for (;;) {
        edu_worker_hit();
        fixture_sleep_ms(15);
    }
    return NULL;
}
#endif

int main(void) {
    int i;

    printf("edu_threads: educational BP-storm lite (%d threads, MIT, safe)\n",
           EDU_THREAD_N);
    fflush(stdout);

#if defined(_WIN32)
    for (i = 0; i < EDU_THREAD_N; i++) {
        HANDLE th = CreateThread(NULL, 0, worker_main, NULL, 0, NULL);
        if (th) {
            CloseHandle(th);
        }
    }
#else
    for (i = 0; i < EDU_THREAD_N; i++) {
        pthread_t th;
        if (pthread_create(&th, NULL, worker_main, NULL) == 0) {
            (void)pthread_detach(th);
        }
    }
#endif

    /* Main thread also hits so a single-thread BP still fires. */
    for (;;) {
        edu_worker_hit();
        fixture_sleep_ms(40);
    }
    return 0;
}
