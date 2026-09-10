/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 *
 * <aio.h> -- picolibc ships none. POSIX asynchronous I/O, implemented
 * SYNCHRONOUSLY: aio_read/aio_write/aio_fsync perform the operation before they
 * return, so the request is always already complete (aio_error == 0,
 * aio_suspend returns at once, aio_cancel reports AIO_ALLDONE). This is a
 * conforming degenerate implementation for the console's blocking I/O.
 * Implemented in runtime/xbox/aio.c.
 */
#ifndef _RXDK_AIO_H_
#define _RXDK_AIO_H_

#include <sys/types.h>
#include <signal.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

struct aiocb {
    int             aio_fildes;
    off_t           aio_offset;
    volatile void  *aio_buf;
    size_t          aio_nbytes;
    int             aio_reqprio;
    struct sigevent aio_sigevent;
    int             aio_lio_opcode;
    /* RXDK internal: the result captured when the request was submitted. */
    ssize_t         __rxdk_ret;
    int             __rxdk_err;
};

#define LIO_READ   0
#define LIO_WRITE  1
#define LIO_NOP    2
#define LIO_NOWAIT 0
#define LIO_WAIT   1

#define AIO_CANCELED    0
#define AIO_NOTCANCELED 1
#define AIO_ALLDONE     2

int     aio_read(struct aiocb *cb);
int     aio_write(struct aiocb *cb);
int     aio_error(const struct aiocb *cb);
ssize_t aio_return(struct aiocb *cb);
int     aio_suspend(const struct aiocb *const list[], int n, const struct timespec *timeout);
int     aio_cancel(int fd, struct aiocb *cb);
int     aio_fsync(int op, struct aiocb *cb);
int     lio_listio(int mode, struct aiocb *const list[], int n, struct sigevent *sig);

#ifdef __cplusplus
}
#endif

#endif /* _RXDK_AIO_H_ */
