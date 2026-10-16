// test_dbg_lock.c - session lock queue keeps MCP off the wait_event thread.
// Module: test (C11).
// Owns: owner/submit/drain ordering assertions for re_dbg_lock.
// Depends: re_test, re_dbg_lock, Threads.
#include "re_test.h"

#include "features/sess/re_dbg_lock.h"

#include <string.h>

#if defined(_WIN32)
#    include <windows.h>
#else
#    include <poll.h>
#    include <pthread.h>
#endif

int re_test_count = 0;
int re_test_fail = 0;

static int g_order[8];
static int g_n;
static int g_owner_ran;

static void push_order(int v) {
    if (g_n < 8)
        g_order[g_n++] = v;
}

static void job_a(void *ctx) {
    (void)ctx;
    push_order(1);
    g_owner_ran = 1;
}

static void job_b(void *ctx) {
    (void)ctx;
    push_order(2);
}

#if defined(_WIN32)
static DWORD WINAPI worker(void *arg) {
#else
static void *worker(void *arg) {
#endif
    (void)arg;
    re_dbg_lock_submit(job_a, NULL);
    re_dbg_lock_submit(job_b, NULL);
    return 0;
}

static void test_queue_on_owner(void) {
    g_n = 0;
    g_owner_ran = 0;
    memset(g_order, 0, sizeof(g_order));
    re_dbg_lock_set_owner(true);
#if defined(_WIN32)
    HANDLE th = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    RE_CHECK(th != NULL);
    /* Drain until both jobs finish. */
    {
        int spins = 0;
        while (g_n < 2 && spins < 200) {
            re_dbg_lock_drain();
            Sleep(5);
            spins++;
        }
    }
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
#else
    pthread_t th;
    RE_CHECK(pthread_create(&th, NULL, worker, NULL) == 0);
    {
        int spins = 0;
        while (g_n < 2 && spins < 200) {
            re_dbg_lock_drain();
            (void)poll(NULL, 0, 5);
            spins++;
        }
    }
    pthread_join(th, NULL);
#endif
    re_dbg_lock_set_owner(false);
    RE_CHECK_EQ_U(g_n, 2);
    RE_CHECK_EQ_U(g_order[0], 1);
    RE_CHECK_EQ_U(g_order[1], 2);
    RE_CHECK(g_owner_ran);
}

static int g_inline_hit;

static void job_inline(void *ctx) {
    (void)ctx;
    g_inline_hit = 1;
}

static void test_inline_without_owner(void) {
    g_inline_hit = 0;
    re_dbg_lock_set_owner(false);
    re_dbg_lock_submit(job_inline, NULL);
    RE_CHECK(g_inline_hit);
}

static int g_nested_drain;

static void job_nested_wait(void *ctx) {
    (void)ctx;
    /* Mirrors wait_event: after_wait -> drain must not deadlock. */
    re_dbg_lock_before_wait();
    re_dbg_lock_after_wait();
    g_nested_drain = 1;
}

static void test_submit_nested_drain(void) {
    g_nested_drain = 0;
    re_dbg_lock_set_owner(false);
    re_dbg_lock_submit(job_nested_wait, NULL);
    RE_CHECK(g_nested_drain);
}

int main(void) {
    test_inline_without_owner();
    test_submit_nested_drain();
    test_queue_on_owner();
    return re_test_report("dbg_lock");
}
