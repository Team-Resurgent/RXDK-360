/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * POSIX threads over the C11 <threads.h> primitives the runtime implements on
 * the kernel (threads.c). pthread_* return an error number directly (0 on
 * success), never via errno. A pthread_t is a small heap control block carrying
 * the underlying C11 thread and the void* return value; pthread_self() finds it
 * through a thread-specific key. A statically-initialised (PTHREAD_*_INITIALIZER)
 * mutex or condition variable is just a zeroed C11 mtx_t/cnd_t, which threads.c
 * lazily initialises from zero on first use under a global lock -- so no
 * per-object init flag is needed and concurrent first-touch is safe.
 *
 * Note: the underlying C11 mutex is a kernel critical section, which is
 * recursive -- so even a PTHREAD_MUTEX_NORMAL mutex lets its owner re-lock
 * rather than deadlock. Cross-thread contention (trylock while another thread
 * holds it) behaves normally and returns EBUSY.
 */
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>

static int map(int rc) {
    switch (rc) {
        case thrd_success:  return 0;
        case thrd_busy:     return EBUSY;
        case thrd_timedout: return ETIMEDOUT;
        case thrd_nomem:    return ENOMEM;
        default:            return EINVAL;
    }
}

/* ---- threads ------------------------------------------------------------- */

struct __pthread {
    thrd_t th;
    void *(*fn)(void *);
    void  *arg;
    void  *ret;
    int    detached;
    int    cancel_req;   /* pthread_cancel was called */
    int    cancel_dis;   /* PTHREAD_CANCEL_DISABLE in effect */
};

static tss_t     g_self_key;
static once_flag g_self_once = ONCE_FLAG_INIT;
static struct __pthread g_main;   /* stands in for the initial thread */

static void self_init(void) {
    tss_create(&g_self_key, (tss_dtor_t)0);
    tss_set(g_self_key, &g_main);
}

static int pt_trampoline(void *p) {
    struct __pthread *c = (struct __pthread *)p;
    call_once(&g_self_once, self_init);
    tss_set(g_self_key, c);
    c->ret = c->fn(c->arg);
    if (c->detached)
        free(c);                  /* nobody will join to free us */
    return 0;
}

int pthread_create(pthread_t *out, const pthread_attr_t *attr,
                   void *(*start)(void *), void *arg) {
    struct __pthread *c;
    int rc;
    if (!out || !start) return EINVAL;
    call_once(&g_self_once, self_init);
    c = (struct __pthread *)malloc(sizeof *c);
    if (!c) return EAGAIN;
    c->fn = start; c->arg = arg; c->ret = 0;
    c->cancel_req = 0; c->cancel_dis = 0;
    c->detached = (attr && attr->detachstate == PTHREAD_CREATE_DETACHED);
    rc = thrd_create(&c->th, pt_trampoline, c);
    if (rc != thrd_success) { free(c); return EAGAIN; }
    if (c->detached) thrd_detach(c->th);
    *out = c;
    return 0;
}

int pthread_join(pthread_t t, void **retval) {
    int r, rc;
    if (!t || t->detached) return EINVAL;
    rc = thrd_join(t->th, &r);
    if (rc != thrd_success) return map(rc);
    if (retval) *retval = t->ret;
    free(t);
    return 0;
}

int pthread_detach(pthread_t t) {
    if (!t) return EINVAL;
    t->detached = 1;
    return map(thrd_detach(t->th));
}

pthread_t pthread_self(void) {
    void *s;
    call_once(&g_self_once, self_init);
    s = tss_get(g_self_key);
    return s ? (pthread_t)s : &g_main;
}

int pthread_equal(pthread_t a, pthread_t b) { return a == b; }

_Noreturn void pthread_exit(void *retval) {
    pthread_t self = pthread_self();
    if (self && self != &g_main) {
        self->ret = retval;
        if (self->detached) free(self);
    }
    thrd_exit(0);
}

int pthread_yield(void) { thrd_yield(); return 0; }

/* ---- cooperative cancellation. There is no safe way to asynchronously unwind
   a running kernel thread, so cancellation is deferred: pthread_cancel sets a
   flag and the target acts on it at the next pthread_testcancel (returning
   PTHREAD_CANCELED to a joiner). ------------------------------------------- */
int pthread_cancel(pthread_t t) {
    if (!t || t == &g_main) return ESRCH;
    t->cancel_req = 1;
    return 0;
}
int pthread_setcancelstate(int state, int *oldstate) {
    pthread_t self = pthread_self();
    if (self == &g_main) { if (oldstate) *oldstate = PTHREAD_CANCEL_ENABLE; return 0; }
    if (oldstate) *oldstate = self->cancel_dis ? PTHREAD_CANCEL_DISABLE : PTHREAD_CANCEL_ENABLE;
    self->cancel_dis = (state == PTHREAD_CANCEL_DISABLE);
    return 0;
}
int pthread_setcanceltype(int type, int *oldtype) {
    /* only deferred cancellation is supported; accept the request either way */
    (void)type;
    if (oldtype) *oldtype = PTHREAD_CANCEL_DEFERRED;
    return 0;
}
void pthread_testcancel(void) {
    pthread_t self = pthread_self();
    if (self != &g_main && self->cancel_req && !self->cancel_dis)
        pthread_exit(PTHREAD_CANCELED);
}

/* ---- attributes ---------------------------------------------------------- */

int pthread_attr_init(pthread_attr_t *a) {
    if (!a) return EINVAL;
    a->detachstate = PTHREAD_CREATE_JOINABLE;
    a->stacksize = 0;
    return 0;
}
int pthread_attr_destroy(pthread_attr_t *a) { (void)a; return 0; }
int pthread_attr_setdetachstate(pthread_attr_t *a, int s) {
    if (!a || (s != PTHREAD_CREATE_JOINABLE && s != PTHREAD_CREATE_DETACHED)) return EINVAL;
    a->detachstate = s; return 0;
}
int pthread_attr_getdetachstate(const pthread_attr_t *a, int *s) {
    if (!a || !s) return EINVAL; *s = a->detachstate; return 0;
}
int pthread_attr_setstacksize(pthread_attr_t *a, size_t n) {
    if (!a) return EINVAL; a->stacksize = n; return 0;   /* advisory */
}
int pthread_attr_getstacksize(const pthread_attr_t *a, size_t *n) {
    if (!a || !n) return EINVAL; *n = a->stacksize; return 0;
}

/* ---- mutex. The C11 mtx auto-initialises from its zeroed state on first
   lock/trylock (threads.c, serialised under a global lock), so a static
   PTHREAD_MUTEX_INITIALIZER just works and there is no per-object init flag. The
   kernel critical section is recursive, so PTHREAD_MUTEX_RECURSIVE is the de
   facto behaviour regardless of the requested type. ------------------------- */

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) {
    (void)a;
    if (!m) return EINVAL;
    return map(mtx_init(&m->__m, mtx_recursive));
}
int pthread_mutex_destroy(pthread_mutex_t *m) {
    if (!m) return EINVAL; mtx_destroy(&m->__m); return 0;
}
int pthread_mutex_lock(pthread_mutex_t *m) {
    if (!m) return EINVAL; return map(mtx_lock(&m->__m));
}
int pthread_mutex_trylock(pthread_mutex_t *m) {
    if (!m) return EINVAL; return map(mtx_trylock(&m->__m));
}
int pthread_mutex_timedlock(pthread_mutex_t *m, const struct timespec *ts) {
    if (!m) return EINVAL; return map(mtx_timedlock(&m->__m, ts));
}
int pthread_mutex_unlock(pthread_mutex_t *m) {
    if (!m) return EINVAL; return map(mtx_unlock(&m->__m));
}
int pthread_mutexattr_init(pthread_mutexattr_t *a) {
    if (!a) return EINVAL; a->type = PTHREAD_MUTEX_DEFAULT; return 0;
}
int pthread_mutexattr_destroy(pthread_mutexattr_t *a) { (void)a; return 0; }
int pthread_mutexattr_settype(pthread_mutexattr_t *a, int t) {
    if (!a) return EINVAL; a->type = t; return 0;
}
int pthread_mutexattr_gettype(const pthread_mutexattr_t *a, int *t) {
    if (!a || !t) return EINVAL; *t = a->type; return 0;
}

/* ---- condition variables (the C11 cnd auto-initialises like the mtx) ------ */

int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a) {
    (void)a;
    if (!c) return EINVAL;
    return map(cnd_init(&c->__c));
}
int pthread_cond_destroy(pthread_cond_t *c) {
    if (!c) return EINVAL; cnd_destroy(&c->__c); return 0;
}
int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) {
    if (!c || !m) return EINVAL;
    return map(cnd_wait(&c->__c, &m->__m));
}
int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m,
                           const struct timespec *ts) {
    if (!c || !m) return EINVAL;
    return map(cnd_timedwait(&c->__c, &m->__m, ts));
}
int pthread_cond_signal(pthread_cond_t *c) {
    if (!c) return EINVAL; return map(cnd_signal(&c->__c));
}
int pthread_cond_broadcast(pthread_cond_t *c) {
    if (!c) return EINVAL; return map(cnd_broadcast(&c->__c));
}
int pthread_condattr_init(pthread_condattr_t *a) { if (!a) return EINVAL; a->__reserved = 0; return 0; }
int pthread_condattr_destroy(pthread_condattr_t *a) { (void)a; return 0; }

/* ---- once ---------------------------------------------------------------- */

int pthread_once(pthread_once_t *once, void (*fn)(void)) {
    if (!once || !fn) return EINVAL;
    call_once(once, fn);
    return 0;
}

/* ---- thread-specific storage --------------------------------------------- */

int pthread_key_create(pthread_key_t *key, void (*dtor)(void *)) {
    if (!key) return EINVAL;
    return map(tss_create(key, (tss_dtor_t)dtor));
}
int pthread_key_delete(pthread_key_t key) { tss_delete(key); return 0; }
void *pthread_getspecific(pthread_key_t key) { return tss_get(key); }
int pthread_setspecific(pthread_key_t key, const void *val) {
    return map(tss_set(key, (void *)val));
}

/* ---- read/write locks (writer-preferring, over a mutex + condvar). The inner
   mutex/cond auto-initialise and __readers/__waitw are zero from the static or
   explicit initializer, so no separate init flag is needed. --------------- */

int pthread_rwlock_init(pthread_rwlock_t *rw, const pthread_rwlockattr_t *a) {
    (void)a;
    if (!rw) return EINVAL;
    pthread_mutex_init(&rw->__m, 0);
    pthread_cond_init(&rw->__c, 0);
    rw->__readers = 0;
    rw->__waitw = 0;
    return 0;
}
int pthread_rwlock_destroy(pthread_rwlock_t *rw) {
    if (!rw) return EINVAL;
    pthread_cond_destroy(&rw->__c);
    pthread_mutex_destroy(&rw->__m);
    return 0;
}
int pthread_rwlock_rdlock(pthread_rwlock_t *rw) {
    if (!rw) return EINVAL;
    pthread_mutex_lock(&rw->__m);
    while (rw->__readers < 0 || rw->__waitw > 0)   /* a writer holds or waits */
        pthread_cond_wait(&rw->__c, &rw->__m);
    rw->__readers++;
    pthread_mutex_unlock(&rw->__m);
    return 0;
}
int pthread_rwlock_tryrdlock(pthread_rwlock_t *rw) {
    int ok;
    if (!rw) return EINVAL;
    pthread_mutex_lock(&rw->__m);
    ok = !(rw->__readers < 0 || rw->__waitw > 0);
    if (ok) rw->__readers++;
    pthread_mutex_unlock(&rw->__m);
    return ok ? 0 : EBUSY;
}
int pthread_rwlock_wrlock(pthread_rwlock_t *rw) {
    if (!rw) return EINVAL;
    pthread_mutex_lock(&rw->__m);
    rw->__waitw++;
    while (rw->__readers != 0)
        pthread_cond_wait(&rw->__c, &rw->__m);
    rw->__waitw--;
    rw->__readers = -1;
    pthread_mutex_unlock(&rw->__m);
    return 0;
}
int pthread_rwlock_trywrlock(pthread_rwlock_t *rw) {
    int ok;
    if (!rw) return EINVAL;
    pthread_mutex_lock(&rw->__m);
    ok = (rw->__readers == 0);
    if (ok) rw->__readers = -1;
    pthread_mutex_unlock(&rw->__m);
    return ok ? 0 : EBUSY;
}
int pthread_rwlock_unlock(pthread_rwlock_t *rw) {
    if (!rw) return EINVAL;
    pthread_mutex_lock(&rw->__m);
    if (rw->__readers < 0) rw->__readers = 0;   /* was a writer */
    else if (rw->__readers > 0) rw->__readers--;
    pthread_cond_broadcast(&rw->__c);
    pthread_mutex_unlock(&rw->__m);
    return 0;
}

/* ---- barriers ------------------------------------------------------------ */

int pthread_barrier_init(pthread_barrier_t *b, const pthread_barrierattr_t *a,
                         unsigned count) {
    (void)a;
    if (!b || count == 0) return EINVAL;
    pthread_mutex_init(&b->__m, 0);
    pthread_cond_init(&b->__c, 0);
    b->__total = count;
    b->__count = 0;
    b->__gen = 0;
    return 0;
}
int pthread_barrier_destroy(pthread_barrier_t *b) {
    if (!b) return EINVAL;
    pthread_cond_destroy(&b->__c);
    pthread_mutex_destroy(&b->__m);
    return 0;
}
int pthread_barrier_wait(pthread_barrier_t *b) {
    unsigned gen;
    int serial = 0;
    if (!b) return EINVAL;
    pthread_mutex_lock(&b->__m);
    gen = b->__gen;
    if (++b->__count == b->__total) {
        b->__gen++;
        b->__count = 0;
        pthread_cond_broadcast(&b->__c);
        serial = 1;
    } else {
        while (gen == b->__gen)
            pthread_cond_wait(&b->__c, &b->__m);
    }
    pthread_mutex_unlock(&b->__m);
    return serial ? PTHREAD_BARRIER_SERIAL_THREAD : 0;
}
