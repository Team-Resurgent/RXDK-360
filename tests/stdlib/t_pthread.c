/* POSIX threads over the C11 kernel threads: create/join with a return value,
   self/equal, mutex contention, trylock, once, thread-specific keys, a
   condition-variable handshake, read/write locks, and a barrier. */
#include "rxdk_test.h"
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include <errno.h>

extern int nanosleep(const struct timespec *, struct timespec *);
static void nap(void) { struct timespec t = {0, 40 * 1000000L}; nanosleep(&t, 0); }

static void *ret_worker(void *a) { return (void *)(intptr_t)(*(int *)a + 1); }

static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;   /* static init */
static int g_counter;
static void *incr_worker(void *a) {
    int i;
    (void)a;
    for (i = 0; i < 1000; i++) {
        pthread_mutex_lock(&g_mtx);
        g_counter++;
        pthread_mutex_unlock(&g_mtx);
    }
    return 0;
}

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static int g_once_count;
static void once_fn(void) { g_once_count++; }
static void *once_worker(void *a) { (void)a; pthread_once(&g_once, once_fn); return 0; }

static pthread_key_t g_key;
static void *tls_worker(void *a) { pthread_setspecific(g_key, a); return pthread_getspecific(g_key); }

static pthread_cond_t  g_cond  = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_cmtx  = PTHREAD_MUTEX_INITIALIZER;
static int g_ready;
static void *cond_waiter(void *a) {
    (void)a;
    pthread_mutex_lock(&g_cmtx);
    while (!g_ready)
        pthread_cond_wait(&g_cond, &g_cmtx);
    pthread_mutex_unlock(&g_cmtx);
    return (void *)(intptr_t)42;
}

static pthread_mutex_t g_tl_mtx;
static int g_tl_result;
static void *trylock_worker(void *a) {
    (void)a;
    g_tl_result = pthread_mutex_trylock(&g_tl_mtx);
    if (g_tl_result == 0) pthread_mutex_unlock(&g_tl_mtx);
    return 0;
}

static pthread_barrier_t g_bar;
static int g_bar_hits;
static pthread_mutex_t g_bmtx = PTHREAD_MUTEX_INITIALIZER;
static void *bar_worker(void *a) {
    (void)a;
    pthread_barrier_wait(&g_bar);
    pthread_mutex_lock(&g_bmtx);
    g_bar_hits++;
    pthread_mutex_unlock(&g_bmtx);
    return 0;
}

int main(void) {
    pthread_t t, ts[4];
    void *rv;
    int in = 41, i;

    /* ---- create / join + return value ---- */
    CHECK_EQI(pthread_create(&t, 0, ret_worker, &in), 0, "pthread_create");
    CHECK_EQI(pthread_join(t, &rv), 0, "pthread_join");
    CHECK_EQI((int)(intptr_t)rv, 42, "join sees the thread's return value");

    /* ---- self / equal ---- */
    CHECK(pthread_equal(pthread_self(), pthread_self()), "pthread_self stable + equal");

    /* ---- mutex contention: 4 x 1000 increments ---- */
    g_counter = 0;
    for (i = 0; i < 4; i++) pthread_create(&ts[i], 0, incr_worker, 0);
    for (i = 0; i < 4; i++) pthread_join(ts[i], 0);
    CHECK_EQI(g_counter, 4000, "mutex-protected counter == 4000 (no lost updates)");

    /* ---- trylock from another thread while main holds it -> EBUSY. (Same-
       thread trylock is not tested: these mutexes are backed by recursive
       kernel critical sections, so the owner re-acquires successfully.) ---- */
    pthread_mutex_init(&g_tl_mtx, 0);
    CHECK_EQI(pthread_mutex_lock(&g_tl_mtx), 0, "mutex_lock");
    pthread_t tl;
    pthread_create(&tl, 0, trylock_worker, 0);
    pthread_join(tl, 0);
    CHECK_EQI(g_tl_result, EBUSY, "trylock from another thread on a held mutex -> EBUSY");
    pthread_mutex_unlock(&g_tl_mtx);
    pthread_mutex_destroy(&g_tl_mtx);

    /* ---- once: fires exactly once across 4 threads ---- */
    g_once_count = 0;
    for (i = 0; i < 4; i++) pthread_create(&ts[i], 0, once_worker, 0);
    for (i = 0; i < 4; i++) pthread_join(ts[i], 0);
    CHECK_EQI(g_once_count, 1, "pthread_once runs exactly once");

    /* ---- thread-specific keys ---- */
    int va = 7, vb = 9;
    void *r1, *r2;
    pthread_t k1, k2;
    pthread_key_create(&g_key, 0);
    pthread_create(&k1, 0, tls_worker, &va);
    pthread_create(&k2, 0, tls_worker, &vb);
    pthread_join(k1, &r1);
    pthread_join(k2, &r2);
    CHECK(r1 == &va && r2 == &vb, "each thread's key value is its own");
    pthread_key_delete(g_key);

    /* ---- condition variable handshake (static PTHREAD_*_INITIALIZER) ---- */
    g_ready = 0;
    pthread_t w;
    void *cr;
    pthread_create(&w, 0, cond_waiter, 0);
    nap();                                  /* let the waiter block */
    pthread_mutex_lock(&g_cmtx);
    g_ready = 1;
    pthread_cond_signal(&g_cond);
    pthread_mutex_unlock(&g_cmtx);
    pthread_join(w, &cr);
    CHECK_EQI((int)(intptr_t)cr, 42, "cond_wait / cond_signal handshake");

    /* ---- read/write lock ---- */
    pthread_rwlock_t rw;
    pthread_rwlock_init(&rw, 0);
    CHECK_EQI(pthread_rwlock_rdlock(&rw), 0, "rwlock rdlock");
    CHECK_EQI(pthread_rwlock_tryrdlock(&rw), 0, "shared second rdlock");
    pthread_rwlock_unlock(&rw);
    pthread_rwlock_unlock(&rw);
    CHECK_EQI(pthread_rwlock_trywrlock(&rw), 0, "trywrlock when free");
    CHECK_EQI(pthread_rwlock_tryrdlock(&rw), EBUSY, "rdlock blocked by a writer -> EBUSY");
    pthread_rwlock_unlock(&rw);
    pthread_rwlock_destroy(&rw);

    /* ---- barrier: 3 threads meet ---- */
    pthread_barrier_init(&g_bar, 0, 3);
    g_bar_hits = 0;
    pthread_t bs[3];
    for (i = 0; i < 3; i++) pthread_create(&bs[i], 0, bar_worker, 0);
    for (i = 0; i < 3; i++) pthread_join(bs[i], 0);
    CHECK_EQI(g_bar_hits, 3, "barrier released all 3 threads");
    pthread_barrier_destroy(&g_bar);

    /* ---- detach ---- */
    pthread_t d;
    pthread_create(&d, 0, ret_worker, &in);
    CHECK_EQI(pthread_detach(d), 0, "pthread_detach");
    nap();                                  /* let the detached thread finish */

    CHECK_DONE("pthread");
    return 0;
}
