/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 *
 * <sys/wait.h> -- thin overlay on picolibc's. picolibc declares wait()/waitpid()
 * and (via our fork) wait4(); it does NOT carry the waitid()/idtype_t interface.
 * A single-title console has no child processes, so waitid() is a linkable stub
 * that fails with ECHILD (see runtime/xbox/posix_unsupported.c). We add the
 * missing declarations here and defer everything else to picolibc.
 */
#ifndef _RXDK_SYS_WAIT_H_
#define _RXDK_SYS_WAIT_H_

#include_next <sys/wait.h>

/* waitid() takes a siginfo_t, which picolibc's <signal.h> only exposes under
   __POSIX_VISIBLE. Guard the whole addition the same way so picolibc's own
   sources (which include us with POSIX visibility off) are unaffected. */
#if __POSIX_VISIBLE
#include <signal.h>   /* siginfo_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    P_ALL = 0,
    P_PID = 1,
    P_PGID = 2
} idtype_t;

/* options for waitid() (also usable with the wait*() options argument) */
#ifndef WEXITED
#define WEXITED    0x04
#endif
#ifndef WSTOPPED
#define WSTOPPED   0x08
#endif
#ifndef WCONTINUED
#define WCONTINUED 0x10
#endif
#ifndef WNOWAIT
#define WNOWAIT    0x01000000
#endif

int waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options);

#ifdef __cplusplus
}
#endif

#endif /* __POSIX_VISIBLE */

#endif /* _RXDK_SYS_WAIT_H_ */
