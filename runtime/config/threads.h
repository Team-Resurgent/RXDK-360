/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * <threads.h> - C11 threads for RXDK-Libs (ISO/IEC 9899:2011 §7.26).
 *
 * Implemented in libc over the Xbox kernel (PsCreateSystemThreadEx + Rtl
 * critical sections + Ke events). Opaque objects are sized byte buffers so this
 * public header pulls in no kernel types; libc statically asserts they fit.
 */
#ifndef _THREADS_H
#define _THREADS_H

#include <time.h> /* struct timespec */

#ifdef __cplusplus
extern "C" {
#endif

/* In C++, thread_local is a built-in keyword; only the C11 library macro form
   is provided here for C translation units. */
#ifndef __cplusplus
#define thread_local _Thread_local
#endif

typedef int (*thrd_start_t)(void*);
typedef void (*tss_dtor_t)(void*);

/* Opaque object types — storage only; layout lives in libc. */
typedef struct __rxdk_thrd* thrd_t;
typedef unsigned int tss_t;
typedef struct {
    unsigned char __opaque[64];
} mtx_t;
typedef struct {
    unsigned char __opaque[64];
} cnd_t;
typedef struct {
    int __state;
} once_flag;

#define ONCE_FLAG_INIT { 0 }
#define TSS_DTOR_ITERATIONS 4

enum {
    mtx_plain = 0,
    mtx_recursive = 1,
    mtx_timed = 2
};

enum {
    thrd_success = 0,
    thrd_busy = 1,
    thrd_error = 2,
    thrd_nomem = 3,
    thrd_timedout = 4
};

/* Threads */
int thrd_create(thrd_t* thr, thrd_start_t func, void* arg);
int thrd_equal(thrd_t a, thrd_t b);
thrd_t thrd_current(void);
int thrd_sleep(const struct timespec* duration, struct timespec* remaining);
void thrd_yield(void);
_Noreturn void thrd_exit(int res);
int thrd_detach(thrd_t thr);
int thrd_join(thrd_t thr, int* res);

/* Mutexes */
int mtx_init(mtx_t* mtx, int type);
int mtx_lock(mtx_t* mtx);
int mtx_timedlock(mtx_t* __restrict mtx, const struct timespec* __restrict ts);
int mtx_trylock(mtx_t* mtx);
int mtx_unlock(mtx_t* mtx);
void mtx_destroy(mtx_t* mtx);

/* Condition variables */
int cnd_init(cnd_t* cond);
int cnd_signal(cnd_t* cond);
int cnd_broadcast(cnd_t* cond);
int cnd_wait(cnd_t* cond, mtx_t* mtx);
int cnd_timedwait(cnd_t* __restrict cond, mtx_t* __restrict mtx,
    const struct timespec* __restrict ts);
void cnd_destroy(cnd_t* cond);

/* Call once */
void call_once(once_flag* flag, void (*func)(void));

/* Thread-specific storage */
int tss_create(tss_t* key, tss_dtor_t dtor);
void* tss_get(tss_t key);
int tss_set(tss_t key, void* val);
void tss_delete(tss_t key);

#ifdef __cplusplus
}
#endif

#endif /* _THREADS_H */
