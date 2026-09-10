/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 *
 * <sys/random.h> -- picolibc ships no such header. getrandom() fills a buffer
 * from picolibc's arc4random PRNG (there is no hardware entropy source on the
 * console). Implemented in runtime/xbox/posix_ext.c.
 */
#ifndef _RXDK_SYS_RANDOM_H_
#define _RXDK_SYS_RANDOM_H_

#include <sys/types.h>

#define GRND_NONBLOCK 0x0001
#define GRND_RANDOM   0x0002

#ifdef __cplusplus
extern "C" {
#endif

ssize_t getrandom(void *buf, size_t buflen, unsigned int flags);

#ifdef __cplusplus
}
#endif

#endif /* _RXDK_SYS_RANDOM_H_ */
