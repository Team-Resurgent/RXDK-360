/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 *
 * POSIX unnamed semaphores (<semaphore.h>) -- picolibc ships no such header, so
 * RXDK supplies one. sem_t is a counting semaphore built on the C11 <threads.h>
 * mutex/condition-variable the runtime already provides (threads.c); the
 * implementation is in runtime/xbox/sem.c. Process-shared semaphores and the
 * named sem_open family are not supported (a title is a single process).
 */
#ifndef _RXDK_SEMAPHORE_H_
#define _RXDK_SEMAPHORE_H_

#include <threads.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    mtx_t m;
    cnd_t c;
    int   count;
} sem_t;

int sem_init(sem_t *sem, int pshared, unsigned int value);
int sem_destroy(sem_t *sem);
int sem_wait(sem_t *sem);
int sem_trywait(sem_t *sem);
int sem_timedwait(sem_t *sem, const struct timespec *abstime);
int sem_post(sem_t *sem);
int sem_getvalue(sem_t *sem, int *sval);

/* Named semaphores need a cross-process namespace the console has not got, so
   the sem_open family is a linkable stub (see runtime/xbox/posix_unsupported.c). */
#define SEM_FAILED ((sem_t *) 0)
sem_t *sem_open(const char *name, int oflag, ...);
int    sem_close(sem_t *sem);
int    sem_unlink(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* _RXDK_SEMAPHORE_H_ */
