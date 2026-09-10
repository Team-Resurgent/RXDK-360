/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * POSIX unnamed semaphores over the C11 <threads.h> primitives the runtime
 * already implements on the kernel (threads.c). A sem_t is a count guarded by a
 * mutex, with waiters parked on a condition variable -- a real, blocking
 * counting semaphore. pshared is accepted but ignored (a title is one process);
 * the named sem_open family is intentionally absent.
 */
#include <semaphore.h>
#include <errno.h>

int sem_init(sem_t *s, int pshared, unsigned int value) {
    (void)pshared;
    if (!s) { errno = EINVAL; return -1; }
    if (mtx_init(&s->m, mtx_plain) != thrd_success) { errno = ENOMEM; return -1; }
    if (cnd_init(&s->c) != thrd_success) { mtx_destroy(&s->m); errno = ENOMEM; return -1; }
    s->count = (int)value;
    return 0;
}

int sem_destroy(sem_t *s) {
    if (!s) { errno = EINVAL; return -1; }
    cnd_destroy(&s->c);
    mtx_destroy(&s->m);
    return 0;
}

int sem_wait(sem_t *s) {
    if (!s) { errno = EINVAL; return -1; }
    mtx_lock(&s->m);
    while (s->count == 0)
        cnd_wait(&s->c, &s->m);
    --s->count;
    mtx_unlock(&s->m);
    return 0;
}

int sem_trywait(sem_t *s) {
    int ok;
    if (!s) { errno = EINVAL; return -1; }
    mtx_lock(&s->m);
    ok = s->count > 0;
    if (ok) --s->count;
    mtx_unlock(&s->m);
    if (!ok) { errno = EAGAIN; return -1; }
    return 0;
}

int sem_timedwait(sem_t *s, const struct timespec *abstime) {
    if (!s) { errno = EINVAL; return -1; }
    mtx_lock(&s->m);
    while (s->count == 0) {
        if (cnd_timedwait(&s->c, &s->m, abstime) == thrd_timedout) {
            mtx_unlock(&s->m);
            errno = ETIMEDOUT;
            return -1;
        }
    }
    --s->count;
    mtx_unlock(&s->m);
    return 0;
}

int sem_post(sem_t *s) {
    if (!s) { errno = EINVAL; return -1; }
    mtx_lock(&s->m);
    ++s->count;
    cnd_signal(&s->c);
    mtx_unlock(&s->m);
    return 0;
}

int sem_getvalue(sem_t *s, int *sval) {
    if (!s || !sval) { errno = EINVAL; return -1; }
    mtx_lock(&s->m);
    *sval = s->count;
    mtx_unlock(&s->m);
    return 0;
}
