/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * C11 <threads.h> for the Xbox 360, over the kernel:
 *   thrd_*  -> ExCreateThread / NtWaitForSingleObjectEx (join) / NtClose
 *   tss_*   -> KeTlsAlloc/Free/GetValue/SetValue (1:1 with kernel TLS)
 *   mtx_*   -> RTL_CRITICAL_SECTION (recursive)
 *   call_once -> a process-wide critical section + flag
 *   cnd_*   -> not implemented yet (no <condition_variable> is built); the C11
 *              backend only references them from <thread>/<mutex>, which we do
 *              not compile. They return thrd_error if ever called.
 *
 * This is what libc++abi's per-thread exception storage sits on (tss + once);
 * the predefined libc locks stay in locks.c.
 */
#include <threads.h>
#include <stdlib.h>

/* xboxkrnl imports (ordinals resolved from the XDK import libraries). */
unsigned ExCreateThread(unsigned *handle, unsigned stack_size, unsigned *tid,
                        unsigned xapi_startup, void *start, void *ctx,
                        unsigned flags);
unsigned NtWaitForSingleObjectEx(unsigned handle, unsigned wait_mode,
                                 unsigned alertable, void *timeout);
unsigned NtClose(unsigned handle);
void     ExTerminateThread(unsigned exit_code);
unsigned KeTlsAlloc(void);
unsigned KeTlsFree(unsigned index);
void    *KeTlsGetValue(unsigned index);
unsigned KeTlsSetValue(unsigned index, void *value);
void     RtlInitializeCriticalSection(void *cs);
void     RtlEnterCriticalSection(void *cs);
void     RtlLeaveCriticalSection(void *cs);
void     KeInitializeEvent(void *ev, unsigned type, unsigned state);
unsigned KeSetEvent(void *ev, int increment, unsigned wait);
unsigned KeWaitForSingleObject(void *obj, unsigned reason, unsigned mode,
                               unsigned alertable, void *timeout);  /* timeout: LARGE_INTEGER*, NULL=infinite */
void     KeQuerySystemTime(unsigned long long *out);  /* 100ns ticks since 1601 */

#define RXDK_THREAD_STACK 0x40000u   /* 256KB, matches the title default */

struct __rxdk_thrd {
    unsigned     handle;
    thrd_start_t func;
    void        *arg;
    int          result;
    int          detached;
};

/* ---- thread-specific storage (tss) over kernel TLS ----------------------- */

#define RXDK_MAX_TSS 32
static struct { unsigned index; tss_dtor_t dtor; int used; } g_tss[RXDK_MAX_TSS];
static unsigned g_reg_cs[8];   /* RTL_CRITICAL_SECTION for the tss/once tables */
static int g_reg_init;

static void reg_lock(void) {
    if (!g_reg_init) { RtlInitializeCriticalSection(&g_reg_cs); g_reg_init = 1; }
    RtlEnterCriticalSection(&g_reg_cs);
}
static void reg_unlock(void) { RtlLeaveCriticalSection(&g_reg_cs); }

int tss_create(tss_t *key, tss_dtor_t dtor) {
    if (!key) return thrd_error;
    unsigned idx = KeTlsAlloc();
    if (idx == 0xFFFFFFFFu) return thrd_error;
    reg_lock();
    for (int i = 0; i < RXDK_MAX_TSS; ++i) {
        if (!g_tss[i].used) {
            g_tss[i].used = 1; g_tss[i].index = idx; g_tss[i].dtor = dtor;
            break;
        }
    }
    reg_unlock();
    *key = idx;
    return thrd_success;
}

void *tss_get(tss_t key)             { return KeTlsGetValue(key); }
int   tss_set(tss_t key, void *val)  { return KeTlsSetValue(key, val) ? thrd_success : thrd_error; }

void tss_delete(tss_t key) {
    reg_lock();
    for (int i = 0; i < RXDK_MAX_TSS; ++i)
        if (g_tss[i].used && g_tss[i].index == key) { g_tss[i].used = 0; break; }
    reg_unlock();
    KeTlsFree(key);
}

/* Run this thread's tss destructors (C11: up to TSS_DTOR_ITERATIONS passes). */
static void run_tss_dtors(void) {
    for (int iter = 0; iter < TSS_DTOR_ITERATIONS; ++iter) {
        int ran = 0;
        for (int i = 0; i < RXDK_MAX_TSS; ++i) {
            if (!g_tss[i].used || !g_tss[i].dtor) continue;
            void *v = KeTlsGetValue(g_tss[i].index);
            if (v) { KeTlsSetValue(g_tss[i].index, 0); g_tss[i].dtor(v); ran = 1; }
        }
        if (!ran) break;
    }
}

/* ---- threads ------------------------------------------------------------- */

static void thread_trampoline(void *ctx) {
    struct __rxdk_thrd *t = (struct __rxdk_thrd *)ctx;
    t->result = t->func(t->arg);
    run_tss_dtors();
    if (t->detached) free(t);
}

int thrd_create(thrd_t *thr, thrd_start_t func, void *arg) {
    if (!thr || !func) return thrd_error;
    struct __rxdk_thrd *t = (struct __rxdk_thrd *)malloc(sizeof(*t));
    if (!t) return thrd_nomem;
    t->func = func; t->arg = arg; t->result = 0; t->detached = 0; t->handle = 0;
    unsigned handle = 0, tid = 0;
    unsigned st = ExCreateThread(&handle, RXDK_THREAD_STACK, &tid, 0,
                                 (void *)thread_trampoline, t, 0);
    if (st != 0) { free(t); return thrd_error; }
    t->handle = handle;
    *thr = t;
    return thrd_success;
}

int thrd_join(thrd_t thr, int *res) {
    if (!thr) return thrd_error;
    NtWaitForSingleObjectEx(thr->handle, 1 /*Kernel*/, 0, 0 /*infinite*/);
    if (res) *res = thr->result;
    NtClose(thr->handle);
    free(thr);
    return thrd_success;
}

int thrd_detach(thrd_t thr) {
    if (!thr) return thrd_error;
    thr->detached = 1;
    NtClose(thr->handle);
    return thrd_success;
}

int    thrd_equal(thrd_t a, thrd_t b) { return a == b; }
thrd_t thrd_current(void)             { return 0; }  /* not tracked for kernel-made threads */
void   thrd_yield(void)               { }
_Noreturn void thrd_exit(int res)     { run_tss_dtors(); ExTerminateThread((unsigned)res); for (;;) {} }

int thrd_sleep(const struct timespec *duration, struct timespec *remaining) {
    (void)duration; (void)remaining;
    return 0;
}

/* ---- mutexes (RTL_CRITICAL_SECTION, recursive) --------------------------- */

/*
 * libc++'s std::mutex is constexpr-constructed to all-zeros (its
 * _LIBCPP_MUTEX_INITIALIZER is `{}`) and its lock path calls mtx_lock directly,
 * never mtx_init -- it assumes a statically-zeroed mutex is usable, the way
 * PTHREAD_MUTEX_INITIALIZER is. An RTL_CRITICAL_SECTION is NOT valid zeroed, so
 * we treat mtx_t as { RTL_CRITICAL_SECTION cs; int inited; } and lazily
 * RtlInitializeCriticalSection on first use (double-checked under the table
 * lock, exactly like cnd_ensure). The 64-byte mtx_t easily holds the ~32-byte
 * critical section plus the flag word at the end.
 */
struct rxdk_mtx {
    unsigned char cs[56];   /* RTL_CRITICAL_SECTION */
    volatile int  inited;
};

static void mtx_ensure(struct rxdk_mtx *m) {
    if (!m->inited) {
        reg_lock();
        if (!m->inited) {
            RtlInitializeCriticalSection(m->cs);
            m->inited = 1;
        }
        reg_unlock();
    }
}

int mtx_init(mtx_t *mtx, int type) {
    (void)type;  /* RTL critical sections are always recursive */
    if (!mtx) return thrd_error;
    struct rxdk_mtx *m = (struct rxdk_mtx *)mtx;
    RtlInitializeCriticalSection(m->cs);
    m->inited = 1;
    return thrd_success;
}
int  mtx_lock(mtx_t *mtx)    { struct rxdk_mtx *m = (struct rxdk_mtx *)mtx; mtx_ensure(m); RtlEnterCriticalSection(m->cs); return thrd_success; }
int  mtx_unlock(mtx_t *mtx)  { struct rxdk_mtx *m = (struct rxdk_mtx *)mtx; mtx_ensure(m); RtlLeaveCriticalSection(m->cs); return thrd_success; }
int  mtx_trylock(mtx_t *mtx) { struct rxdk_mtx *m = (struct rxdk_mtx *)mtx; mtx_ensure(m); RtlEnterCriticalSection(m->cs); return thrd_success; }
void mtx_destroy(mtx_t *mtx) { (void)mtx; }  /* no RtlDeleteCriticalSection on the 360 */
int  mtx_timedlock(mtx_t *__restrict m, const struct timespec *__restrict t) {
    (void)t; mtx_lock(m); return thrd_success;
}

/* ---- call_once ----------------------------------------------------------- */

static unsigned g_once_cs[8];
static int g_once_init;

void call_once(once_flag *flag, void (*func)(void)) {
    if (!g_once_init) { RtlInitializeCriticalSection(&g_once_cs); g_once_init = 1; }
    RtlEnterCriticalSection(&g_once_cs);
    if (flag->__state == 0) { func(); flag->__state = 1; }
    RtlLeaveCriticalSection(&g_once_cs);
}

/* ---- condition variables (KEVENT auto-reset + waiter count) -------------- */

struct rxdk_cnd {
    unsigned char ev[16];   /* X_KEVENT (DISPATCHER_HEADER) */
    volatile int  inited;
    volatile int  waiters;
};

/* std::condition_variable is constexpr-constructed (zeroed), never cnd_init'd,
   so lazily initialise the KEVENT on first use, under the table lock. */
static void cnd_ensure(struct rxdk_cnd *c) {
    if (!c->inited) {
        reg_lock();
        if (!c->inited) {
            KeInitializeEvent(c->ev, 1 /*Synchronization: auto-reset*/, 0);
            c->waiters = 0;
            c->inited = 1;
        }
        reg_unlock();
    }
}

int cnd_init(cnd_t *cond) {
    struct rxdk_cnd *c = (struct rxdk_cnd *)cond;
    if (!c) return thrd_error;
    KeInitializeEvent(c->ev, 1, 0);
    c->waiters = 0;
    c->inited = 1;
    return thrd_success;
}

int cnd_wait(cnd_t *cond, mtx_t *mtx) {
    struct rxdk_cnd *c = (struct rxdk_cnd *)cond;
    if (!c || !mtx) return thrd_error;
    cnd_ensure(c);
    c->waiters++;                 /* caller holds mtx */
    mtx_unlock(mtx);
    KeWaitForSingleObject(c->ev, 0, 0, 0, 0);
    mtx_lock(mtx);
    c->waiters--;
    return thrd_success;
}

/* 1601->1970 epoch offset in 100ns units. */
#define RXDK_EPOCH_100NS 116444736000000000ULL

int cnd_timedwait(cnd_t *__restrict cond, mtx_t *__restrict mtx,
                  const struct timespec *__restrict ts) {
    struct rxdk_cnd *c = (struct rxdk_cnd *)cond;
    if (!c || !mtx) return thrd_error;
    if (!ts) return cnd_wait(cond, mtx);
    cnd_ensure(c);
    /* Convert the absolute (TIME_UTC) deadline to a relative kernel timeout. */
    unsigned long long now100;
    KeQuerySystemTime(&now100);
    unsigned long long now = now100 - RXDK_EPOCH_100NS;  /* since 1970, 100ns */
    unsigned long long deadline =
        (unsigned long long)ts->tv_sec * 10000000ULL + (unsigned long long)ts->tv_nsec / 100ULL;
    long long rel = (long long)(deadline - now);
    if (rel <= 0) return thrd_timedout;
    long long timeout = -rel;     /* negative = relative, in 100ns units */
    c->waiters++;
    mtx_unlock(mtx);
    unsigned st = KeWaitForSingleObject(c->ev, 0, 0, 0, &timeout);
    mtx_lock(mtx);
    c->waiters--;
    return st == 0x00000102u /*STATUS_TIMEOUT*/ ? thrd_timedout : thrd_success;
}

int cnd_signal(cnd_t *cond) {
    struct rxdk_cnd *c = (struct rxdk_cnd *)cond;
    if (!c) return thrd_error;
    cnd_ensure(c);
    if (c->waiters > 0) KeSetEvent(c->ev, 0, 0);
    return thrd_success;
}

int cnd_broadcast(cnd_t *cond) {
    struct rxdk_cnd *c = (struct rxdk_cnd *)cond;
    if (!c) return thrd_error;
    cnd_ensure(c);
    for (int n = c->waiters; n > 0; --n) KeSetEvent(c->ev, 0, 0);
    return thrd_success;
}

void cnd_destroy(cnd_t *cond) { (void)cond; }
