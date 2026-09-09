/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * Picolibc stdin/stdout/stderr for Xbox PE/lld.
 * Vendor posixiob_*.c uses __weak_reference for stdout et al.; lld does not
 * apply those aliases, so we provide strong FILE* globals here instead.
 */
#include "local-stdio.h"

static char read_buf[BUFSIZ];
static struct __file_bufio __stdin = FDEV_SETUP_POSIX(0, read_buf, BUFSIZ, __SRD, 0);
FILE *const __posix_stdin = &__stdin.xfile.cfile.file;
FILE *const stdin = &__stdin.xfile.cfile.file;

static char write_buf[BUFSIZ];
static struct __file_bufio __stdout = FDEV_SETUP_POSIX(1, write_buf, BUFSIZ, __SWR, __BLBF);
FILE *const __posix_stdout = &__stdout.xfile.cfile.file;
FILE *const stdout = &__stdout.xfile.cfile.file;

/*
 * stderr is LINE-buffered (not unbuffered). C leaves stderr "not fully buffered",
 * which permits line buffering, and on Xbox it is required: the debug-console sink
 * (write() -> DbgPrint in fileio.c) turns every write() call into one discrete debug
 * event, so an unbuffered stderr (a 1-byte buffer flushing every char) makes
 * fprintf(stderr, "...\n") arrive one character per line in the debug monitor / VS
 * Output pane. A full buffer + __BLBF flushes on each '\n' instead, so a whole line
 * is emitted per event -- matching stdout. Partial (newline-less) output is flushed
 * at exit by the destructor below.
 */
#ifndef __PICOLIBC_STDERR_BUFSIZ
#define __PICOLIBC_STDERR_BUFSIZ BUFSIZ
#endif

static char err_buf[__PICOLIBC_STDERR_BUFSIZ];
static struct __file_bufio __stderr =
    FDEV_SETUP_POSIX(2, err_buf, __PICOLIBC_STDERR_BUFSIZ, __SWR, __BLBF);
FILE *const __posix_stderr = &__stderr.xfile.cfile.file;
FILE *const stderr = &__stderr.xfile.cfile.file;

__attribute__((destructor(101))) static void posix_stdio_exit(void)
{
    if (stdout)
        fflush(stdout);
    /* stderr is line-buffered: flush any partial (newline-less) final line at exit. */
    if (stderr)
        fflush(stderr);
}
