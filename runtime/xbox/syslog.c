/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * syslog() for the console. There is no syslogd, but the 360 has a writable
 * utility drive and a kernel debug channel, so this is a real implementation:
 * each record is formatted "ident[pid]: <SEVERITY> message" and both appended
 * to cache:\syslog.log (best effort -- skipped if the drive is not mounted) and
 * mirrored to DbgPrint so it shows live in the debug monitor / xenia log.
 */
#define _GNU_SOURCE 1
#include <syslog.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

extern int DbgPrint(const char *fmt, ...);

#define RXDK_SYSLOG_PATH "cache:\\syslog.log"

static const char *g_ident   = "";
static int         g_option  = 0;
static int         g_facility = LOG_USER;
static int         g_mask    = 0xff;   /* all severities enabled */
static int         g_fd      = -1;     /* log-file descriptor, lazily opened */

static const char *pri_name(int pri) {
    switch (LOG_PRI(pri)) {
        case LOG_EMERG:   return "EMERG";
        case LOG_ALERT:   return "ALERT";
        case LOG_CRIT:    return "CRIT";
        case LOG_ERR:     return "ERR";
        case LOG_WARNING: return "WARNING";
        case LOG_NOTICE:  return "NOTICE";
        case LOG_INFO:    return "INFO";
        default:          return "DEBUG";
    }
}

static void ensure_open(void) {
    if (g_fd < 0)
        g_fd = open(RXDK_SYSLOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
}

void openlog(const char *ident, int option, int facility) {
    g_ident   = ident ? ident : "";
    g_option  = option;
    if (facility)
        g_facility = facility;
    if (option & LOG_NDELAY)          /* open the log now rather than on first use */
        ensure_open();
}

void vsyslog(int priority, const char *format, va_list ap) {
    char msg[512];
    char line[608];
    int  n;

    if (!(LOG_MASK(LOG_PRI(priority)) & g_mask))
        return;                        /* filtered out by setlogmask */

    vsnprintf(msg, sizeof msg, format, ap);
    if (g_option & LOG_PID)
        n = snprintf(line, sizeof line, "%s[%d]: <%s> %s\n",
                     g_ident, getpid(), pri_name(priority), msg);
    else
        n = snprintf(line, sizeof line, "%s: <%s> %s\n",
                     g_ident, pri_name(priority), msg);
    if (n < 0)
        return;
    if (n >= (int)sizeof line)
        n = (int)sizeof line - 1;

    DbgPrint("%s", line);              /* live mirror */

    ensure_open();                     /* persistent log on the utility drive */
    if (g_fd >= 0)
        write(g_fd, line, (size_t)n);
}

void syslog(int priority, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vsyslog(priority, format, ap);
    va_end(ap);
}

void closelog(void) {
    if (g_fd >= 0) { close(g_fd); g_fd = -1; }
    g_ident = "";
    g_option = 0;
    g_facility = LOG_USER;
}

int setlogmask(int mask) {
    int old = g_mask;
    if (mask != 0)                     /* mask 0 queries without changing */
        g_mask = mask;
    return old;
}
