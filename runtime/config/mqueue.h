/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 *
 * <mqueue.h> -- picolibc ships none. POSIX message queues are an inter-process
 * IPC mechanism; a title is a single process, so these are linkable stubs that
 * fail with ENOSYS (see runtime/xbox/posix_unsupported.c).
 */
#ifndef _RXDK_MQUEUE_H_
#define _RXDK_MQUEUE_H_

#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int mqd_t;

struct mq_attr {
    long mq_flags;
    long mq_maxmsg;
    long mq_msgsize;
    long mq_curmsgs;
};

mqd_t   mq_open(const char *name, int oflag, ...);
int     mq_close(mqd_t mqdes);
int     mq_unlink(const char *name);
int     mq_send(mqd_t mqdes, const char *msg, size_t len, unsigned int prio);
ssize_t mq_receive(mqd_t mqdes, char *msg, size_t len, unsigned int *prio);
int     mq_getattr(mqd_t mqdes, struct mq_attr *attr);
int     mq_setattr(mqd_t mqdes, const struct mq_attr *n, struct mq_attr *o);

#ifdef __cplusplus
}
#endif

#endif /* _RXDK_MQUEUE_H_ */
