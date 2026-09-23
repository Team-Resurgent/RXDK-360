/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 *
 * <sys/wait.h> -- thin passthrough to picolibc's. The consolidated picolibc
 * 'xbox' branch now carries the waitid()/idtype_t interface (and the WEXITED/
 * WSTOPPED/WCONTINUED/WNOWAIT option bits) that this overlay used to add, so the
 * additions were removed to avoid a redefinition clash; waitid() is still a
 * linkable ECHILD stub in runtime/xbox/posix_unsupported.c (a single-title console
 * has no child processes). The overlay is retained as a passthrough so the include
 * slot stays available for any future 360-specific tweak.
 */
#ifndef _RXDK_SYS_WAIT_H_
#define _RXDK_SYS_WAIT_H_

#include_next <sys/wait.h>

#endif /* _RXDK_SYS_WAIT_H_ */
