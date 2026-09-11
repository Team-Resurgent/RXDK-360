/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * Time backend for picolibc on the Xbox 360 kernel. Ported from the original
 * Xbox's runtime (RXDK-Libs libs/libc/xbox/timeio.c); the one platform change is
 * the monotonic source.
 *
 *   - wall clock  -> KeQuerySystemTime           (100ns ticks since 1601-01-01)
 *   - monotonic   -> the PPC time-base register (mftb) + KeQueryPerformanceFrequency
 *
 * The original Xbox reads its monotonic counter through the KeQueryPerformance-
 * Counter kernel export; the 360 has no such export -- the performance counter
 * IS the time-base register, read in-line with mftb (xenia emits it), scaled by
 * KeQueryPerformanceFrequency (the time-base rate). picolibc routes time()
 * through gettimeofday() and clock() through times(); we also override clock()
 * directly off the performance counter. Kernel-only dependencies, no libxapi.
 *
 * libc++'s <chrono> (built with _LIBCPP_HAS_CLOCK_GETTIME) uses clock_gettime
 * for system_clock (CLOCK_REALTIME) and steady_clock (CLOCK_MONOTONIC).
 */

/* Expose the full clock API (clockid_t, CLOCK_MONOTONIC, clock_gettime).
   picolibc gates these behind __POSIX_VISIBLE/__GNU_VISIBLE, both off under
   -std=c23; CLOCK_MONOTONIC specifically needs __GNU_VISIBLE. */
#define _GNU_SOURCE 1

#include <errno.h>
#include <stddef.h>
#include <sys/time.h>
#include <sys/times.h>
#include <time.h>

void KeQuerySystemTime(unsigned long long *out);       /* 100ns ticks since 1601 */

/* KeQueryPerformanceFrequency returns a full 64-bit rate in a single register
   (MS ABI). This ILP32 clang target would mis-read a `long long` return as an
   r3:r4 pair, so read it through the assembly thunk (ke_perf.S), which captures
   the whole register and writes it to memory. See docs/ilp32-ppc64-abi-plan.md. */
void rxdk_query_perf_freq64(unsigned long long *out);  /* time-base rate (Hz) */

/* 100ns intervals between 1601-01-01 and 1970-01-01 (FILETIME -> Unix epoch). */
#define RXDK_EPOCH_DIFF_100NS 116444736000000000ULL
#define RXDK_100NS_PER_SEC    10000000ULL

/* The PPC time-base register: a free-running monotonic counter incrementing at
   KeQueryPerformanceFrequency Hz. It is 64-bit, but this is an ILP32 target, so
   an "=r" long long is a 32-bit register PAIR -- a single `mftb` would fill only
   one half and leave the other garbage. Read it the portable 32-bit way: upper,
   lower, upper again, retrying if the low half rolled over between the reads. */
static inline unsigned long long read_timebase(void)
{
    unsigned int hi, lo, hi2;
    do {
        __asm__ __volatile__("mftbu %0" : "=r"(hi));
        __asm__ __volatile__("mftb  %0" : "=r"(lo));
        __asm__ __volatile__("mftbu %0" : "=r"(hi2));
    } while (hi != hi2);
    return ((unsigned long long)hi << 32) | lo;
}

static unsigned long long unix_100ns(void)
{
    unsigned long long v;
    KeQuerySystemTime(&v);
    return (v > RXDK_EPOCH_DIFF_100NS) ? (v - RXDK_EPOCH_DIFF_100NS) : 0;
}

int gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    if (tv) {
        unsigned long long t = unix_100ns();
        tv->tv_sec = (time_t)(t / RXDK_100NS_PER_SEC);
        tv->tv_usec = (suseconds_t)((t % RXDK_100NS_PER_SEC) / 10ULL);
    }
    return 0;
}

int clock_gettime(clockid_t clk_id, struct timespec *tp)
{
    if (!tp) { errno = EFAULT; return -1; }

    if (clk_id == CLOCK_MONOTONIC) {
        unsigned long long freq; rxdk_query_perf_freq64(&freq);
        unsigned long long c = read_timebase();
        if (freq == 0) { tp->tv_sec = 0; tp->tv_nsec = 0; return 0; }
        tp->tv_sec = (time_t)(c / freq);
        tp->tv_nsec = (long)(((c % freq) * 1000000000ULL) / freq);
        return 0;
    }

    /* CLOCK_REALTIME and anything else: wall clock. */
    {
        unsigned long long t = unix_100ns();
        tp->tv_sec = (time_t)(t / RXDK_100NS_PER_SEC);
        tp->tv_nsec = (long)((t % RXDK_100NS_PER_SEC) * 100ULL);
    }
    return 0;
}

int clock_getres(clockid_t clk_id, struct timespec *tp)
{
    (void)clk_id;
    if (tp) {
        tp->tv_sec = 0;
        tp->tv_nsec = 100; /* KeQuerySystemTime granularity */
    }
    return 0;
}

/* clock_nanosleep: relative sleep (flags 0) forwards to nanosleep; an absolute
   deadline (TIMER_ABSTIME) is turned into the remaining interval off the same
   clock. Returns 0 or a positive errno (it does NOT set errno), per POSIX. */
extern int nanosleep(const struct timespec *req, struct timespec *rem);

int clock_nanosleep(clockid_t clk_id, int flags,
                    const struct timespec *rqtp, struct timespec *rmtp)
{
    if (!rqtp || rqtp->tv_nsec < 0 || rqtp->tv_nsec >= 1000000000L)
        return EINVAL;

    if (flags & TIMER_ABSTIME) {
        struct timespec now, delta;
        clock_gettime(clk_id, &now);
        delta.tv_sec = rqtp->tv_sec - now.tv_sec;
        delta.tv_nsec = rqtp->tv_nsec - now.tv_nsec;
        if (delta.tv_nsec < 0) { delta.tv_nsec += 1000000000L; delta.tv_sec--; }
        if (delta.tv_sec < 0 || (delta.tv_sec == 0 && delta.tv_nsec <= 0))
            return 0;                       /* deadline already passed */
        return nanosleep(&delta, NULL) == 0 ? 0 : errno;  /* abs sleep: no remainder */
    }

    return nanosleep(rqtp, rmtp) == 0 ? 0 : errno;
}

clock_t clock(void)
{
    unsigned long long freq; rxdk_query_perf_freq64(&freq);
    unsigned long long c, sec, rem;

    if (freq == 0)
        return (clock_t)-1;

    c = read_timebase();
    sec = c / freq;
    rem = c % freq;
    /* split to avoid 64-bit overflow on c * CLOCKS_PER_SEC */
    return (clock_t)(sec * (unsigned long long)CLOCKS_PER_SEC
                     + (rem * (unsigned long long)CLOCKS_PER_SEC) / freq);
}

clock_t times(struct tms *buf)
{
    /* CLK_TCK == CLOCKS_PER_SEC here, so times() shares clock()'s scale. There
       is no per-process CPU accounting on Xbox (one dedicated title), so the
       elapsed monotonic time is reported as user time; the rest are zero. */
    clock_t t = clock();

    if (buf) {
        buf->tms_utime = t;
        buf->tms_stime = 0;
        buf->tms_cutime = 0;
        buf->tms_cstime = 0;
    }
    return t;
}
