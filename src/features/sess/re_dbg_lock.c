// re_dbg_lock.c - process-wide session lock and MCP job queue.
// Module: feature (C11).
// Owns: mutex, cond, owner id, and the FIFO of queued MCP jobs.
// Depends: re_dbg_lock.h. Jobs are stack-allocated by the submitter.
#include "features/sess/re_dbg_lock.h"

#include <string.h>

#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
typedef DWORD re_tid_t;
static CRITICAL_SECTION g_mu;
static CONDITION_VARIABLE g_cv;
static int g_mu_ready;
#else
#    include <pthread.h>
typedef pthread_t re_tid_t;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
#endif

typedef struct re_dbg_job {
    re_dbg_job_fn fn;
    void *ctx;
    volatile int done;
    struct re_dbg_job *next;
} re_dbg_job_t;

static re_dbg_job_t *g_head;
static re_dbg_job_t *g_tail;
static int g_owned;
static re_tid_t g_owner;
static int g_owner_set;

static re_tid_t self_id(void) {
#if defined(_WIN32)
    return GetCurrentThreadId();
#else
    return pthread_self();
#endif
}

static int tid_eq(re_tid_t a, re_tid_t b) {
#if defined(_WIN32)
    return a == b;
#else
    return pthread_equal(a, b);
#endif
}

static void mu_init(void) {
#if defined(_WIN32)
    if (!g_mu_ready) {
        InitializeCriticalSection(&g_mu);
        InitializeConditionVariable(&g_cv);
        g_mu_ready = 1;
    }
#endif
}

static void mu_lock(void) {
    mu_init();
#if defined(_WIN32)
    EnterCriticalSection(&g_mu);
#else
    pthread_mutex_lock(&g_mu);
#endif
}

static void mu_unlock(void) {
#if defined(_WIN32)
    LeaveCriticalSection(&g_mu);
#else
    pthread_mutex_unlock(&g_mu);
#endif
}

static void mu_wait(void) {
#if defined(_WIN32)
    SleepConditionVariableCS(&g_cv, &g_mu, INFINITE);
#else
    pthread_cond_wait(&g_cv, &g_mu);
#endif
}

static void mu_signal(void) {
#if defined(_WIN32)
    WakeAllConditionVariable(&g_cv);
#else
    pthread_cond_broadcast(&g_cv);
#endif
}

static int is_owner_thread(void) {
    return g_owned && g_owner_set && tid_eq(g_owner, self_id());
}

/* Run queued jobs without holding the mutex. wait_event tools call
 * re_dbg_lock_after_wait -> drain; nesting under a held lock deadlocks. */
static void run_queue_holding(void) {
    while (g_head) {
        re_dbg_job_t *j = g_head;
        g_head = j->next;
        if (!g_head)
            g_tail = NULL;
        j->next = NULL;
        mu_unlock();
        if (j->fn)
            j->fn(j->ctx);
        mu_lock();
        j->done = 1;
        mu_signal();
    }
}

void re_dbg_lock_set_owner(bool on) {
    mu_lock();
    g_owned = on ? 1 : 0;
    if (on) {
        g_owner = self_id();
        g_owner_set = 1;
    } else {
        g_owner_set = 0;
        run_queue_holding();
    }
    mu_unlock();
}

void re_dbg_lock_acquire(void) {
    mu_lock();
}

void re_dbg_lock_release(void) {
    mu_unlock();
}

void re_dbg_lock_drain(void) {
    mu_lock();
    run_queue_holding();
    mu_unlock();
}

void re_dbg_lock_before_wait(void) {
    /* MCP may enqueue while the OS wait blocks; never hold the mutex here. */
}

void re_dbg_lock_after_wait(void) {
    re_dbg_lock_drain();
}

void re_dbg_lock_submit(re_dbg_job_fn fn, void *ctx) {
    re_dbg_job_t job;
    if (!fn)
        return;
    memset(&job, 0, sizeof(job));
    job.fn = fn;
    job.ctx = ctx;
    mu_lock();
    if (g_owned && !is_owner_thread()) {
        job.next = NULL;
        if (g_tail)
            g_tail->next = &job;
        else
            g_head = &job;
        g_tail = &job;
        while (!job.done)
            mu_wait();
        mu_unlock();
        return;
    }
    /* Inline path: release before fn so wait_event can drain safely. */
    mu_unlock();
    fn(ctx);
}
