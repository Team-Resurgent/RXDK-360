/*
 * picolibc retargetable locking on the Xbox 360 kernel.
 *
 * The C library serialises its internals (malloc, stdio buffers, atexit, env,
 * ...) through these __retarget_lock_* hooks plus a set of predefined static
 * lock objects. Each lock is a kernel RTL_CRITICAL_SECTION (recursive), lazily
 * initialised on first acquire -- safe because every predefined lock is first
 * touched on the main thread before any worker spawns. So the modern runtime is
 * thread-safe: a title that spawns threads gets real mutual exclusion around the
 * libc internals, not a no-op. Kernel-only (RtlInitializeCriticalSection /
 * RtlEnterCriticalSection / RtlLeaveCriticalSection are xboxkrnl exports; there
 * is no RtlDeleteCriticalSection on the 360, so close() just frees).
 */
#include <stdlib.h>
#include <sys/lock.h>

/* xboxkrnl exports (resolved as imports). RTL_CRITICAL_SECTION is 28 bytes on
   the 360; we carry an opaque, over-aligned buffer for it. */
void RtlInitializeCriticalSection(void *cs);
void RtlEnterCriticalSection(void *cs);
void RtlLeaveCriticalSection(void *cs);

struct __lock {
    int inited;
    unsigned int cs[8];              /* >= sizeof(RTL_CRITICAL_SECTION) (28), 4-aligned */
};

/* Predefined recursive mutexes picolibc references by name. */
struct __lock __lock___libc_recursive_mutex;
struct __lock __lock___malloc_recursive_mutex;
struct __lock __lock___sfp_recursive_mutex;
struct __lock __lock___atexit_recursive_mutex;
struct __lock __lock___at_quick_exit_mutex;
struct __lock __lock___env_recursive_mutex;
struct __lock __lock___tz_mutex;
struct __lock __lock___arc4random_mutex;

static void ensure(struct __lock *l) {
    if (!l->inited) {
        RtlInitializeCriticalSection(&l->cs);
        l->inited = 1;
    }
}

void __retarget_lock_init(_LOCK_T *lock) {
    struct __lock *l = (struct __lock *)malloc(sizeof(*l));
    if (l) {
        l->inited = 0;
        ensure(l);
    }
    *lock = l;
}

void __retarget_lock_init_recursive(_LOCK_T *lock) { __retarget_lock_init(lock); }

void __retarget_lock_close(_LOCK_T lock) { free(lock); }
void __retarget_lock_close_recursive(_LOCK_T lock) { free(lock); }

void __retarget_lock_acquire(_LOCK_T lock) { ensure(lock); RtlEnterCriticalSection(&lock->cs); }
void __retarget_lock_acquire_recursive(_LOCK_T lock) { ensure(lock); RtlEnterCriticalSection(&lock->cs); }

void __retarget_lock_release(_LOCK_T lock) { RtlLeaveCriticalSection(&lock->cs); }
void __retarget_lock_release_recursive(_LOCK_T lock) { RtlLeaveCriticalSection(&lock->cs); }
