/*
 * Storage/setters for the libc I/O + process hooks, plus the minimal POSIX
 * backend (read/write/lseek/close/execve) that routes stdin/stdout/stderr
 * through them. picolibc's stdio (posix_stdio_streams.c) calls write(1)/read(0);
 * with no hook installed, output goes to the kernel debug monitor (DbgPrint) and
 * input is EOF -- matching RXDK-Libs.
 */
#include "libc_hooks.h"

extern int DbgPrint(const char *fmt, ...);

rxdk_stdin_fn  __rxdk_stdin_hook  = 0;
rxdk_output_fn __rxdk_output_hook = 0;
rxdk_exec_fn   __rxdk_exec_hook   = 0;

void rxdk_set_stdin_handler(rxdk_stdin_fn fn)   { __rxdk_stdin_hook = fn; }
void rxdk_set_output_handler(rxdk_output_fn fn) { __rxdk_output_hook = fn; }
void rxdk_set_exec_handler(rxdk_exec_fn fn)     { __rxdk_exec_hook = fn; }

ssize_t write(int fd, const void *buf, size_t count) {
    if (__rxdk_output_hook)
        return __rxdk_output_hook(fd, buf, count);
    if (fd == 1 || fd == 2) {           /* default sink: the debug monitor */
        DbgPrint("%.*s", (int)count, (const char *)buf);
        return (ssize_t)count;
    }
    return -1;
}

ssize_t read(int fd, void *buf, size_t count) {
    if (fd == 0)
        return __rxdk_stdin_hook ? __rxdk_stdin_hook(buf, count) : 0;  /* EOF */
    return -1;
}

off_t lseek(int fd, off_t offset, int whence) {
    (void)fd; (void)offset; (void)whence;
    return -1;
}

int close(int fd) { (void)fd; return 0; }

int execve(const char *path, char *const argv[], char *const envp[]) {
    if (__rxdk_exec_hook)
        return __rxdk_exec_hook(path, argv, envp);
    (void)path; (void)argv; (void)envp;
    return -1;                          /* ENOSYS */
}
