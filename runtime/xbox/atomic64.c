/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * 64-bit atomic runtime support (the __atomic_*_8 libcalls).
 *
 * The Xenon is a 64-bit core, but titles are built ILP32 (powerpc-unknown-
 * xbox360), and in the 32-bit PowerPC ABI a 64-bit atomic is not guaranteed
 * lock-free, so clang lowers std::atomic<64-bit> / atomic<T> where sizeof(T)==8
 * to these out-of-line calls instead of inline larx/stcx.. libc++'s
 * <barrier>/<semaphore>/atomic-wait use 64-bit atomic counters, so without these
 * a concurrency title fails to link (__atomic_fetch_add_8 &c undefined).
 *
 * Implemented over a single global spinlock built on the 4-byte atomics, which
 * ARE lock-free inline on this target (lwarx/stwcx.). Correct for the general
 * multi-hardware-thread case (the 360 runs 6); the 64-bit ops here are low-
 * contention counter updates, so a global lock is fine. This mirrors what
 * compiler-rt's libatomic does for a size the target can't do lock-free.
 */
#include <stdint.h>
#include <stdbool.h>

static volatile int __rxdk_atomic8_lock = 0;

static inline void a8_lock(void)
{
    while (__atomic_exchange_n(&__rxdk_atomic8_lock, 1, __ATOMIC_ACQUIRE)) {
        /* spin read-only until it looks free, to avoid hammering the reservation */
        while (__atomic_load_n(&__rxdk_atomic8_lock, __ATOMIC_RELAXED)) { }
    }
}

static inline void a8_unlock(void)
{
    __atomic_store_n(&__rxdk_atomic8_lock, 0, __ATOMIC_RELEASE);
}

uint64_t __atomic_load_8(const volatile void *ptr, int memorder)
{
    (void)memorder;
    a8_lock();
    uint64_t v = *(const volatile uint64_t *)ptr;
    a8_unlock();
    return v;
}

void __atomic_store_8(volatile void *ptr, uint64_t val, int memorder)
{
    (void)memorder;
    a8_lock();
    *(volatile uint64_t *)ptr = val;
    a8_unlock();
}

uint64_t __atomic_exchange_8(volatile void *ptr, uint64_t val, int memorder)
{
    (void)memorder;
    a8_lock();
    volatile uint64_t *p = (volatile uint64_t *)ptr;
    uint64_t old = *p;
    *p = val;
    a8_unlock();
    return old;
}

bool __atomic_compare_exchange_8(volatile void *ptr, void *expected,
                                 uint64_t desired, int success, int failure)
{
    (void)success;
    (void)failure;
    a8_lock();
    volatile uint64_t *p = (volatile uint64_t *)ptr;
    uint64_t *exp = (uint64_t *)expected;
    bool ok = (*p == *exp);
    if (ok)
        *p = desired;
    else
        *exp = *p;
    a8_unlock();
    return ok;
}

#define RXDK_ATOMIC8_FETCH(name, expr)                                      \
    uint64_t __atomic_fetch_##name##_8(volatile void *ptr, uint64_t val,    \
                                       int memorder)                        \
    {                                                                       \
        (void)memorder;                                                     \
        a8_lock();                                                          \
        volatile uint64_t *p = (volatile uint64_t *)ptr;                    \
        uint64_t old = *p;                                                  \
        *p = (expr);                                                        \
        a8_unlock();                                                        \
        return old;                                                         \
    }

RXDK_ATOMIC8_FETCH(add, old + val)
RXDK_ATOMIC8_FETCH(sub, old - val)
RXDK_ATOMIC8_FETCH(and, old & val)
RXDK_ATOMIC8_FETCH(or, old | val)
RXDK_ATOMIC8_FETCH(xor, old ^ val)
RXDK_ATOMIC8_FETCH(nand, ~(old & val))
