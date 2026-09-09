/*
 * Storage/setters for the libc I/O + process hooks, plus execve. The POSIX
 * read/write/lseek/close backend -- console fds through these hooks, real files
 * over the kernel -- lives in fileio.c now; this file only owns the hook state
 * and the process (execve) hook. picolibc's stdio calls write(1)/read(0); with
 * no hook installed, fileio.c routes output to the debug monitor and input to
 * EOF, matching RXDK-Libs.
 */
#include "libc_hooks.h"

rxdk_stdin_fn  __rxdk_stdin_hook  = 0;
rxdk_output_fn __rxdk_output_hook = 0;
rxdk_exec_fn   __rxdk_exec_hook   = 0;

void rxdk_set_stdin_handler(rxdk_stdin_fn fn)   { __rxdk_stdin_hook = fn; }
void rxdk_set_output_handler(rxdk_output_fn fn) { __rxdk_output_hook = fn; }
void rxdk_set_exec_handler(rxdk_exec_fn fn)     { __rxdk_exec_hook = fn; }

int execve(const char *path, char *const argv[], char *const envp[]) {
    if (__rxdk_exec_hook)
        return __rxdk_exec_hook(path, argv, envp);
    (void)path; (void)argv; (void)envp;
    return -1;                          /* ENOSYS */
}
