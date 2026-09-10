/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 *
 * <sys/shm.h> -- picolibc ships none. System V shared memory segments live in a
 * cross-process namespace a single-title console has no concept of; linkable
 * stubs fail with ENOSYS (see runtime/xbox/posix_unsupported.c).
 */
#ifndef _RXDK_SYS_SHM_H_
#define _RXDK_SYS_SHM_H_

#include <sys/ipc.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long shmatt_t;

struct shmid_ds {
    struct ipc_perm shm_perm;
    size_t          shm_segsz;
    time_t          shm_atime;
    time_t          shm_dtime;
    time_t          shm_ctime;
    pid_t           shm_cpid;
    pid_t           shm_lpid;
    shmatt_t        shm_nattch;
};

/* shmat() flags */
#define SHM_RDONLY 010000
#define SHM_RND    020000

int    shmget(key_t key, size_t size, int shmflg);
void  *shmat(int shmid, const void *shmaddr, int shmflg);
int    shmdt(const void *shmaddr);
int    shmctl(int shmid, int cmd, struct shmid_ds *buf);

#ifdef __cplusplus
}
#endif

#endif /* _RXDK_SYS_SHM_H_ */
