/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 *
 * <syslog.h> -- picolibc ships none. RXDK routes syslog() to a log file on the
 * writable utility drive (cache:\syslog.log) and mirrors each line to the
 * kernel debug channel so it is visible live. Implemented in runtime/xbox/
 * syslog.c.
 */
#ifndef _RXDK_SYSLOG_H_
#define _RXDK_SYSLOG_H_

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* priorities (severity) */
#define LOG_EMERG   0
#define LOG_ALERT   1
#define LOG_CRIT    2
#define LOG_ERR     3
#define LOG_WARNING 4
#define LOG_NOTICE  5
#define LOG_INFO    6
#define LOG_DEBUG   7
#define LOG_PRIMASK 0x07
#define LOG_PRI(p)  ((p) & LOG_PRIMASK)

/* facilities (encoded in bits 3..) */
#define LOG_KERN     (0 << 3)
#define LOG_USER     (1 << 3)
#define LOG_MAIL     (2 << 3)
#define LOG_DAEMON   (3 << 3)
#define LOG_AUTH     (4 << 3)
#define LOG_SYSLOG   (5 << 3)
#define LOG_LPR      (6 << 3)
#define LOG_LOCAL0   (16 << 3)
#define LOG_LOCAL7   (23 << 3)
#define LOG_FACMASK  0x03f8
#define LOG_MAKEPRI(fac, pri) (((fac) & LOG_FACMASK) | ((pri) & LOG_PRIMASK))

/* openlog options */
#define LOG_PID    0x01
#define LOG_CONS   0x02
#define LOG_ODELAY 0x04
#define LOG_NDELAY 0x08
#define LOG_NOWAIT 0x10
#define LOG_PERROR 0x20

/* setlogmask helpers */
#define LOG_MASK(pri) (1 << (pri))
#define LOG_UPTO(pri) ((1 << ((pri) + 1)) - 1)

void openlog(const char *ident, int option, int facility);
void syslog(int priority, const char *format, ...);
void vsyslog(int priority, const char *format, va_list ap);
void closelog(void);
int  setlogmask(int mask);

#ifdef __cplusplus
}
#endif

#endif /* _RXDK_SYSLOG_H_ */
