/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * A COOPERATIVE signal facility + the ITIMER_REAL interval timer.
 *
 * The Xbox 360 cannot asynchronously interrupt a running thread, so there is no
 * true POSIX signal delivery. What is real and useful, though, is:
 *   - signal()/sigaction() register a disposition per signal;
 *   - raise() and kill(self) run the installed handler synchronously in the
 *     caller;
 *   - setitimer(ITIMER_REAL)/alarm() arm a helper thread that runs the SIGALRM
 *     handler when the timer expires.
 * The deviation from POSIX is that a handler runs cooperatively (in the raising
 * thread, or on the timer helper thread) rather than pre-empting the main
 * thread, and it cannot interrupt a blocking call. That is enough for the common
 * uses -- a periodic SIGALRM tick, a cleanup handler invoked via raise(), abort
 * on SIGABRT. sa_sigaction/SA_SIGINFO and signal masking are not delivered.
 */
#define _GNU_SOURCE 1
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <threads.h>

extern void abort(void);
extern int  clock_gettime(clockid_t clk, struct timespec *tp);

#define RXDK_NSIG 32
#define NS 1000000000L

static _sig_func_ptr g_handler[RXDK_NSIG];   /* SIG_DFL (0) initialised */

/* Run a signal's installed disposition. Synchronous -- see the file comment. */
void __rxdk_deliver_signal(int sig) {
    _sig_func_ptr h;
    if (sig <= 0 || sig >= RXDK_NSIG)
        return;
    h = g_handler[sig];
    if (h == SIG_IGN)
        return;
    if (h == SIG_DFL) {
        if (sig == SIGABRT)
            abort();                 /* the one default action we can honour */
        return;                      /* no process to terminate: ignore the rest */
    }
    h(sig);
}

_sig_func_ptr signal(int sig, _sig_func_ptr handler) {
    _sig_func_ptr old;
    if (sig <= 0 || sig >= RXDK_NSIG) { errno = EINVAL; return SIG_ERR; }
    old = g_handler[sig];
    g_handler[sig] = handler;
    return old;
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *old) {
    if (sig <= 0 || sig >= RXDK_NSIG) { errno = EINVAL; return -1; }
    if (old) {
        old->sa_handler = g_handler[sig];
        old->sa_flags = 0;
        old->sa_mask = 0;
    }
    if (act)
        g_handler[sig] = act->sa_handler;   /* SA_SIGINFO form is not delivered */
    return 0;
}

int raise(int sig) {
    if (sig <= 0 || sig >= RXDK_NSIG) { errno = EINVAL; return -1; }
    __rxdk_deliver_signal(sig);
    return 0;
}

int kill(pid_t pid, int sig) {
    if (pid == 1 || pid == getpid()) {      /* the single title can signal itself */
        if (sig < 0 || sig >= RXDK_NSIG) { errno = EINVAL; return -1; }
        if (sig != 0) __rxdk_deliver_signal(sig);
        return 0;
    }
    errno = ESRCH;                          /* no other process exists */
    return -1;
}

/* ---- ITIMER_REAL: one per-process real-time interval timer ------------- */

static struct timespec ts_add(struct timespec a, struct timespec b) {
    struct timespec r; r.tv_sec = a.tv_sec + b.tv_sec; r.tv_nsec = a.tv_nsec + b.tv_nsec;
    if (r.tv_nsec >= NS) { r.tv_nsec -= NS; r.tv_sec++; } return r;
}
static struct timespec ts_sub(struct timespec a, struct timespec b) {
    struct timespec r; r.tv_sec = a.tv_sec - b.tv_sec; r.tv_nsec = a.tv_nsec - b.tv_nsec;
    if (r.tv_nsec < 0) { r.tv_nsec += NS; r.tv_sec--; } return r;
}
static int ts_expired(struct timespec r) { return r.tv_sec < 0 || (r.tv_sec == 0 && r.tv_nsec <= 0); }
static int ts_zero(struct timespec r) { return r.tv_sec == 0 && r.tv_nsec == 0; }
static struct timespec tv2ts(struct timeval v) { struct timespec t; t.tv_sec = v.tv_sec; t.tv_nsec = v.tv_usec * 1000L; return t; }
static struct timeval  ts2tv(struct timespec t) { struct timeval v; v.tv_sec = t.tv_sec; v.tv_usec = t.tv_nsec / 1000L; return v; }

static mtx_t     it_mtx;
static cnd_t     it_cnd;
static thrd_t    it_thr;
static once_flag it_once = ONCE_FLAG_INIT;
static int       it_run;
static int       it_armed;
static struct timespec it_next;       /* absolute CLOCK_MONOTONIC */
static struct timespec it_interval;

static int itimer_thread(void *arg) {
    (void)arg;
    mtx_lock(&it_mtx);
    while (it_run) {
        struct timespec now, rem, rt, dl;
        if (!it_armed) { cnd_wait(&it_cnd, &it_mtx); continue; }
        clock_gettime(CLOCK_MONOTONIC, &now);
        rem = ts_sub(it_next, now);
        if (ts_expired(rem)) {
            if (!ts_zero(it_interval)) it_next = ts_add(it_next, it_interval);
            else                       it_armed = 0;
            mtx_unlock(&it_mtx);
            __rxdk_deliver_signal(SIGALRM);       /* handler runs on this thread */
            mtx_lock(&it_mtx);
            continue;
        }
        clock_gettime(CLOCK_REALTIME, &rt);
        dl = ts_add(rt, rem);
        cnd_timedwait(&it_cnd, &it_mtx, &dl);
    }
    mtx_unlock(&it_mtx);
    return 0;
}

static void itimer_init(void) {
    mtx_init(&it_mtx, mtx_plain);
    cnd_init(&it_cnd);
    it_run = 1;
    thrd_create(&it_thr, itimer_thread, 0);
}

int setitimer(int which, const struct itimerval *nv, struct itimerval *ov) {
    struct timespec val, now;
    if (which != ITIMER_REAL) { errno = EINVAL; return -1; }  /* no CPU-time timers */
    if (!nv) { errno = EINVAL; return -1; }
    call_once(&it_once, itimer_init);
    mtx_lock(&it_mtx);
    if (ov) {
        ov->it_interval = ts2tv(it_interval);
        if (it_armed) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            struct timespec rem = ts_sub(it_next, now);
            if (ts_expired(rem)) { rem.tv_sec = 0; rem.tv_nsec = 0; }
            ov->it_value = ts2tv(rem);
        } else { ov->it_value.tv_sec = 0; ov->it_value.tv_usec = 0; }
    }
    it_interval = tv2ts(nv->it_interval);
    val = tv2ts(nv->it_value);
    if (ts_zero(val)) {
        it_armed = 0;
    } else {
        clock_gettime(CLOCK_MONOTONIC, &now);
        it_next = ts_add(now, val);
        it_armed = 1;
    }
    cnd_signal(&it_cnd);
    mtx_unlock(&it_mtx);
    return 0;
}

int getitimer(int which, struct itimerval *cur) {
    struct timespec now;
    if (which != ITIMER_REAL) { errno = EINVAL; return -1; }
    if (!cur) { errno = EINVAL; return -1; }
    call_once(&it_once, itimer_init);
    mtx_lock(&it_mtx);
    cur->it_interval = ts2tv(it_interval);
    if (it_armed) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        struct timespec rem = ts_sub(it_next, now);
        if (ts_expired(rem)) { rem.tv_sec = 0; rem.tv_nsec = 0; }
        cur->it_value = ts2tv(rem);
    } else { cur->it_value.tv_sec = 0; cur->it_value.tv_usec = 0; }
    mtx_unlock(&it_mtx);
    return 0;
}

unsigned int alarm(unsigned int seconds) {
    struct itimerval nv, ov;
    nv.it_interval.tv_sec = 0; nv.it_interval.tv_usec = 0;
    nv.it_value.tv_sec = (time_t)seconds; nv.it_value.tv_usec = 0;
    if (setitimer(ITIMER_REAL, &nv, &ov) != 0)
        return 0;
    return (unsigned int)ov.it_value.tv_sec + (ov.it_value.tv_usec ? 1u : 0u);
}
