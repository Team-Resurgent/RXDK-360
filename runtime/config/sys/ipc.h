/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 *
 * <sys/ipc.h> -- picolibc ships none. System V IPC (shared memory, message
 * queues, semaphore sets) is a cross-process namespace; a title is a single
 * process, so the whole family is linkable stubs failing with ENOSYS -- see
 * runtime/xbox/posix_unsupported.c. This header carries the shared IPC types
 * and flags used by <sys/shm.h>, <sys/msg.h> and <sys/sem.h>.
 */
#ifndef _RXDK_SYS_IPC_H_
#define _RXDK_SYS_IPC_H_

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int key_t;

struct ipc_perm {
    key_t         __key;
    uid_t         uid;
    gid_t         gid;
    uid_t         cuid;
    gid_t         cgid;
    unsigned short mode;
    unsigned short __seq;
};

/* ipcget() key */
#define IPC_PRIVATE ((key_t)0)

/* flags for the *get() calls (OR'd into the mode) */
#define IPC_CREAT  01000
#define IPC_EXCL   02000
#define IPC_NOWAIT 04000

/* control commands for the *ctl() calls */
#define IPC_RMID 0
#define IPC_SET  1
#define IPC_STAT 2

key_t ftok(const char *path, int id);

#ifdef __cplusplus
}
#endif

#endif /* _RXDK_SYS_IPC_H_ */
