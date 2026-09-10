/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 *
 * <sys/msg.h> -- picolibc ships none. System V message queues are inter-process
 * IPC; a title is a single process, so linkable stubs fail with ENOSYS (see
 * runtime/xbox/posix_unsupported.c).
 */
#ifndef _RXDK_SYS_MSG_H_
#define _RXDK_SYS_MSG_H_

#include <sys/ipc.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long msgqnum_t;
typedef unsigned long msglen_t;

struct msqid_ds {
    struct ipc_perm msg_perm;
    msgqnum_t       msg_qnum;
    msglen_t        msg_qbytes;
    pid_t           msg_lspid;
    pid_t           msg_lrpid;
    time_t          msg_stime;
    time_t          msg_rtime;
    time_t          msg_ctime;
};

/* the caller defines the payload; this is the canonical header shape */
struct msgbuf {
    long mtype;
    char mtext[1];
};

/* msgrcv() flag */
#define MSG_NOERROR 010000

int     msgget(key_t key, int msgflg);
int     msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg);
ssize_t msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg);
int     msgctl(int msqid, int cmd, struct msqid_ds *buf);

#ifdef __cplusplus
}
#endif

#endif /* _RXDK_SYS_MSG_H_ */
