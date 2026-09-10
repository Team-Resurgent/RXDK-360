/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * POSIX asynchronous I/O, implemented synchronously: each aio_read/aio_write/
 * aio_fsync runs the operation via pread/pwrite/fsync before returning, storing
 * the result in the control block, so the request is always already complete.
 * A conforming degenerate implementation for the console's blocking I/O -- no
 * background threads, no completion signal delivered.
 */
#define _GNU_SOURCE 1     /* expose struct sigevent (used by struct aiocb) */
#include <aio.h>
#include <errno.h>

extern ssize_t pread(int fd, void *buf, size_t count, off_t offset);
extern ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
extern int     fsync(int fd);

static int submit(struct aiocb *cb, int write) {
    ssize_t r;
    if (!cb) { errno = EINVAL; return -1; }
    if (write)
        r = pwrite(cb->aio_fildes, (const void *)cb->aio_buf, cb->aio_nbytes, cb->aio_offset);
    else
        r = pread(cb->aio_fildes, (void *)cb->aio_buf, cb->aio_nbytes, cb->aio_offset);
    cb->__rxdk_ret = r;
    cb->__rxdk_err = (r < 0) ? errno : 0;
    return 0;
}

int aio_read(struct aiocb *cb)  { return submit(cb, 0); }
int aio_write(struct aiocb *cb) { return submit(cb, 1); }

int aio_error(const struct aiocb *cb) {
    if (!cb) { errno = EINVAL; return -1; }
    return cb->__rxdk_err;                 /* 0 = completed successfully */
}

ssize_t aio_return(struct aiocb *cb) {
    if (!cb) { errno = EINVAL; return -1; }
    return cb->__rxdk_ret;
}

int aio_suspend(const struct aiocb *const list[], int n, const struct timespec *timeout) {
    (void)list; (void)n; (void)timeout;
    return 0;                              /* everything already completed */
}

int aio_cancel(int fd, struct aiocb *cb) {
    (void)fd; (void)cb;
    return AIO_ALLDONE;                    /* nothing outstanding to cancel */
}

int aio_fsync(int op, struct aiocb *cb) {
    (void)op;
    if (!cb) { errno = EINVAL; return -1; }
    if (fsync(cb->aio_fildes) != 0) { cb->__rxdk_ret = -1; cb->__rxdk_err = errno; return -1; }
    cb->__rxdk_ret = 0;
    cb->__rxdk_err = 0;
    return 0;
}

int lio_listio(int mode, struct aiocb *const list[], int n, struct sigevent *sig) {
    int i;
    (void)mode; (void)sig;
    if (!list) { errno = EINVAL; return -1; }
    for (i = 0; i < n; i++) {
        struct aiocb *cb = list[i];
        if (!cb || cb->aio_lio_opcode == LIO_NOP)
            continue;
        submit(cb, cb->aio_lio_opcode == LIO_WRITE);
    }
    return 0;                              /* synchronous: all done on return */
}
