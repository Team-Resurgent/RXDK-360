/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 *
 * <sys/sem.h> -- picolibc ships none. System V semaphore SETS are inter-process
 * IPC; a title is a single process, so linkable stubs fail with ENOSYS (see
 * runtime/xbox/posix_unsupported.c). The real, in-process POSIX semaphores are
 * <semaphore.h> / runtime/xbox/sem.c.
 */
#ifndef _RXDK_SYS_SEM_H_
#define _RXDK_SYS_SEM_H_

#include <sys/ipc.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct semid_ds {
    struct ipc_perm  sem_perm;
    unsigned short   sem_nsems;
    time_t           sem_otime;
    time_t           sem_ctime;
};

struct sembuf {
    unsigned short sem_num;
    short          sem_op;
    short          sem_flg;
};

/* sembuf.sem_flg */
#define SEM_UNDO 010000

/* semctl() commands */
#define GETPID  11
#define GETVAL  12
#define GETALL  13
#define GETNCNT 14
#define GETZCNT 15
#define SETVAL  16
#define SETALL  17

int semget(key_t key, int nsems, int semflg);
int semop(int semid, struct sembuf *sops, size_t nsops);
int semctl(int semid, int semnum, int cmd, ...);

#ifdef __cplusplus
}
#endif

#endif /* _RXDK_SYS_SEM_H_ */
