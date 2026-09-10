/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 *
 * <pthread.h> -- picolibc ships none. RXDK provides POSIX threads as a thin
 * layer over the C11 <threads.h> primitives the runtime already implements on
 * the kernel (threads.c). Implemented in runtime/xbox/pthread.c.
 *
 * Covered: thread create/join/detach/self/equal/exit, mutex (+attr, recursive),
 * condition variables (+attr), once, thread-specific keys, read/write locks and
 * barriers. Not covered: cancellation, scheduling policy/priority, process-
 * shared objects, and robust mutexes (a title is a single process).
 */
#ifndef _RXDK_PTHREAD_H_
#define _RXDK_PTHREAD_H_

#include <threads.h>
#include <time.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __pthread *pthread_t;

typedef struct { int detachstate; size_t stacksize; }  pthread_attr_t;
typedef struct { int type; }                            pthread_mutexattr_t;
typedef struct { int __reserved; }                        pthread_condattr_t;
typedef struct { int __reserved; }                        pthread_rwlockattr_t;
typedef struct { int __reserved; }                        pthread_barrierattr_t;

/* The underlying C11 mtx_t/cnd_t lazily self-initialise from a zeroed state on
   first use (threads.c, under a global lock), so the static initializers below
   just zero them -- no per-object init flag is needed. */
typedef struct { mtx_t __m; }                           pthread_mutex_t;
typedef struct { cnd_t __c; }                           pthread_cond_t;
typedef tss_t                                           pthread_key_t;
typedef once_flag                                       pthread_once_t;

typedef struct {
    pthread_mutex_t __m;
    pthread_cond_t  __c;
    int __readers;   /* >=0: reader count; -1: a writer holds it */
    int __waitw;     /* writers waiting (for writer preference) */
} pthread_rwlock_t;

typedef struct {
    pthread_mutex_t __m;
    pthread_cond_t  __c;
    unsigned __total, __count, __gen;
} pthread_barrier_t;

#define PTHREAD_MUTEX_INITIALIZER  { { { 0 } } }
#define PTHREAD_COND_INITIALIZER   { { { 0 } } }
#define PTHREAD_RWLOCK_INITIALIZER { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0 }
#define PTHREAD_ONCE_INIT          ONCE_FLAG_INIT

#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1
#define PTHREAD_MUTEX_NORMAL    0
#define PTHREAD_MUTEX_RECURSIVE 1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_DEFAULT   0
#define PTHREAD_BARRIER_SERIAL_THREAD (-1)

#define PTHREAD_CANCEL_ENABLE        0
#define PTHREAD_CANCEL_DISABLE       1
#define PTHREAD_CANCEL_DEFERRED      0
#define PTHREAD_CANCEL_ASYNCHRONOUS  1
#define PTHREAD_CANCELED ((void *) -1)

/* ---- threads ---- */
int       pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int       pthread_join(pthread_t, void **);
int       pthread_detach(pthread_t);
pthread_t pthread_self(void);
int       pthread_equal(pthread_t, pthread_t);
_Noreturn void pthread_exit(void *);
int       pthread_yield(void);

/* ---- cooperative cancellation (deferred; checked at pthread_testcancel) ---- */
int  pthread_cancel(pthread_t);
int  pthread_setcancelstate(int state, int *oldstate);
int  pthread_setcanceltype(int type, int *oldtype);
void pthread_testcancel(void);

/* ---- attr ---- */
int pthread_attr_init(pthread_attr_t *);
int pthread_attr_destroy(pthread_attr_t *);
int pthread_attr_setdetachstate(pthread_attr_t *, int);
int pthread_attr_getdetachstate(const pthread_attr_t *, int *);
int pthread_attr_setstacksize(pthread_attr_t *, size_t);
int pthread_attr_getstacksize(const pthread_attr_t *, size_t *);

/* ---- mutex ---- */
int pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int pthread_mutex_destroy(pthread_mutex_t *);
int pthread_mutex_lock(pthread_mutex_t *);
int pthread_mutex_trylock(pthread_mutex_t *);
int pthread_mutex_timedlock(pthread_mutex_t *, const struct timespec *);
int pthread_mutex_unlock(pthread_mutex_t *);
int pthread_mutexattr_init(pthread_mutexattr_t *);
int pthread_mutexattr_destroy(pthread_mutexattr_t *);
int pthread_mutexattr_settype(pthread_mutexattr_t *, int);
int pthread_mutexattr_gettype(const pthread_mutexattr_t *, int *);

/* ---- condition variables ---- */
int pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
int pthread_cond_destroy(pthread_cond_t *);
int pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
int pthread_cond_timedwait(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
int pthread_cond_signal(pthread_cond_t *);
int pthread_cond_broadcast(pthread_cond_t *);
int pthread_condattr_init(pthread_condattr_t *);
int pthread_condattr_destroy(pthread_condattr_t *);

/* ---- once ---- */
int pthread_once(pthread_once_t *, void (*)(void));

/* ---- thread-specific storage ---- */
int   pthread_key_create(pthread_key_t *, void (*)(void *));
int   pthread_key_delete(pthread_key_t);
void *pthread_getspecific(pthread_key_t);
int   pthread_setspecific(pthread_key_t, const void *);

/* ---- read/write locks ---- */
int pthread_rwlock_init(pthread_rwlock_t *, const pthread_rwlockattr_t *);
int pthread_rwlock_destroy(pthread_rwlock_t *);
int pthread_rwlock_rdlock(pthread_rwlock_t *);
int pthread_rwlock_tryrdlock(pthread_rwlock_t *);
int pthread_rwlock_wrlock(pthread_rwlock_t *);
int pthread_rwlock_trywrlock(pthread_rwlock_t *);
int pthread_rwlock_unlock(pthread_rwlock_t *);

/* ---- barriers ---- */
int pthread_barrier_init(pthread_barrier_t *, const pthread_barrierattr_t *, unsigned);
int pthread_barrier_destroy(pthread_barrier_t *);
int pthread_barrier_wait(pthread_barrier_t *);

/* ---- fork handlers: a title never fork()s, so registration is a no-op that
   always "succeeds" -- see runtime/xbox/posix_unsupported.c ---- */
int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void));

#ifdef __cplusplus
}
#endif

#endif /* _RXDK_PTHREAD_H_ */
