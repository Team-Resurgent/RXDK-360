/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * POSIX per-process timers (timer_create/settime/gettime/delete). The console
 * delivers no signals, so SIGEV_SIGNAL is not supported -- but SIGEV_THREAD
 * (deliver each expiration by calling a notification function) and SIGEV_NONE
 * (no notification; poll with timer_gettime) need no signals and are real here:
 * each timer owns a helper thread (C11 threads.c) that sleeps on a condition
 * variable until the next expiration, invokes the callback, and re-arms for a
 * periodic interval. The SIGEV_THREAD callback runs on the timer's own helper
 * thread (serialised), not a fresh thread per tick.
 */
#define _GNU_SOURCE 1
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <threads.h>

extern int clock_gettime(clockid_t clk, struct timespec *tp);   /* clock.c */

#define NS 1000000000L

static struct timespec ts_add(struct timespec a, struct timespec b) {
    struct timespec r;
    r.tv_sec = a.tv_sec + b.tv_sec;
    r.tv_nsec = a.tv_nsec + b.tv_nsec;
    if (r.tv_nsec >= NS) { r.tv_nsec -= NS; r.tv_sec++; }
    return r;
}
static struct timespec ts_sub(struct timespec a, struct timespec b) {
    struct timespec r;
    r.tv_sec = a.tv_sec - b.tv_sec;
    r.tv_nsec = a.tv_nsec - b.tv_nsec;
    if (r.tv_nsec < 0) { r.tv_nsec += NS; r.tv_sec--; }
    return r;
}
static int ts_expired(struct timespec r) {           /* <= 0 ? */
    return r.tv_sec < 0 || (r.tv_sec == 0 && r.tv_nsec <= 0);
}
static int ts_zero(struct timespec r) { return r.tv_sec == 0 && r.tv_nsec == 0; }

/* Invoke the SIGEV_THREAD callback. Calling it through its real
   void(*)(union sigval) type makes this MS-PPC clang emit an ELFv2 r2/TOC
   global-entry prologue (referencing .TOC., which the non-TOC link does not
   provide) purely because the argument is a union passed by value. union sigval
   is a single pointer-word here, so we call through a void*-argument signature
   instead -- ABI-identical (the value arrives in the same register) and free of
   the spurious TOC setup. */
static void rxdk_timer_fire(void (*fn)(union sigval), union sigval v) {
    ((void (*)(void *))fn)(v.sival_ptr);
}

struct rxdk_timer {
    int   notify;                 /* SIGEV_NONE | SIGEV_THREAD */
    void (*fn)(union sigval);
    union sigval val;
    clockid_t clk;

    mtx_t  mtx;
    cnd_t  cnd;
    thrd_t thr;
    int    running;               /* helper thread keeps looping */
    int    armed;                 /* a next-expiration is set */
    struct timespec next;         /* absolute next fire, in clk's timescale */
    struct timespec interval;     /* periodic reload (zero = one-shot) */
};

static int timer_thread(void *arg) {
    struct rxdk_timer *t = (struct rxdk_timer *)arg;
    mtx_lock(&t->mtx);
    while (t->running) {
        struct timespec now, rem, rt, deadline;
        if (!t->armed) { cnd_wait(&t->cnd, &t->mtx); continue; }

        clock_gettime(t->clk, &now);
        rem = ts_sub(t->next, now);
        if (ts_expired(rem)) {                        /* fire */
            void (*fn)(union sigval) = t->fn;
            union sigval v = t->val;
            int notify = t->notify;
            if (!ts_zero(t->interval)) t->next = ts_add(t->next, t->interval);
            else                       t->armed = 0;
            mtx_unlock(&t->mtx);
            if (notify == SIGEV_THREAD && fn) rxdk_timer_fire(fn, v);  /* outside the lock */
            mtx_lock(&t->mtx);
            continue;
        }
        /* our cnd_timedwait takes an absolute CLOCK_REALTIME deadline; convert
           the remaining interval accordingly, and re-loop when settime/delete
           signals us early. */
        clock_gettime(CLOCK_REALTIME, &rt);
        deadline = ts_add(rt, rem);
        cnd_timedwait(&t->cnd, &t->mtx, &deadline);
    }
    mtx_unlock(&t->mtx);
    return 0;
}

int timer_create(clockid_t clk, struct sigevent *sev, timer_t *timerid) {
    struct rxdk_timer *t;
    int notify = sev ? sev->sigev_notify : SIGEV_SIGNAL;

    if (!timerid) { errno = EINVAL; return -1; }
    if (notify == SIGEV_SIGNAL) { errno = ENOTSUP; return -1; }  /* no signals */
    if (notify != SIGEV_NONE && notify != SIGEV_THREAD) { errno = EINVAL; return -1; }

    t = (struct rxdk_timer *)calloc(1, sizeof *t);
    if (!t) { errno = ENOMEM; return -1; }
    t->notify = notify;
    t->clk = clk;
    if (notify == SIGEV_THREAD) {
        t->fn  = sev->sigev_notify_function;
        t->val = sev->sigev_value;
    }
    mtx_init(&t->mtx, mtx_plain);
    cnd_init(&t->cnd);
    t->running = 1;
    if (thrd_create(&t->thr, timer_thread, t) != thrd_success) {
        cnd_destroy(&t->cnd); mtx_destroy(&t->mtx); free(t);
        errno = EAGAIN; return -1;
    }
    *timerid = (timer_t)(uintptr_t)t;
    return 0;
}

int timer_settime(timer_t timerid, int flags, const struct itimerspec *value,
                  struct itimerspec *ovalue) {
    struct rxdk_timer *t = (struct rxdk_timer *)(uintptr_t)timerid;
    if (!t || !value) { errno = EINVAL; return -1; }
    mtx_lock(&t->mtx);
    if (ovalue) {
        ovalue->it_interval = t->interval;
        if (t->armed) {
            struct timespec now;
            clock_gettime(t->clk, &now);
            ovalue->it_value = ts_sub(t->next, now);
            if (ts_expired(ovalue->it_value)) { ovalue->it_value.tv_sec = 0; ovalue->it_value.tv_nsec = 0; }
        } else {
            ovalue->it_value.tv_sec = 0; ovalue->it_value.tv_nsec = 0;
        }
    }
    t->interval = value->it_interval;
    if (ts_zero(value->it_value)) {
        t->armed = 0;                                 /* disarm */
    } else if (flags & TIMER_ABSTIME) {
        t->next = value->it_value;                    /* already absolute */
        t->armed = 1;
    } else {
        struct timespec now;
        clock_gettime(t->clk, &now);
        t->next = ts_add(now, value->it_value);
        t->armed = 1;
    }
    cnd_signal(&t->cnd);                              /* wake the helper to recompute */
    mtx_unlock(&t->mtx);
    return 0;
}

int timer_gettime(timer_t timerid, struct itimerspec *value) {
    struct rxdk_timer *t = (struct rxdk_timer *)(uintptr_t)timerid;
    if (!t || !value) { errno = EINVAL; return -1; }
    mtx_lock(&t->mtx);
    value->it_interval = t->interval;
    if (t->armed) {
        struct timespec now;
        clock_gettime(t->clk, &now);
        value->it_value = ts_sub(t->next, now);
        if (ts_expired(value->it_value)) { value->it_value.tv_sec = 0; value->it_value.tv_nsec = 0; }
    } else {
        value->it_value.tv_sec = 0; value->it_value.tv_nsec = 0;
    }
    mtx_unlock(&t->mtx);
    return 0;
}

int timer_delete(timer_t timerid) {
    struct rxdk_timer *t = (struct rxdk_timer *)(uintptr_t)timerid;
    if (!t) { errno = EINVAL; return -1; }
    mtx_lock(&t->mtx);
    t->running = 0;
    t->armed = 0;
    cnd_signal(&t->cnd);
    mtx_unlock(&t->mtx);
    thrd_join(t->thr, NULL);
    cnd_destroy(&t->cnd);
    mtx_destroy(&t->mtx);
    free(t);
    return 0;
}
