/* syslog(): records are appended to cache:\syslog.log (and mirrored to the debug
   channel). This reads the log file back to confirm the ident/pid/severity/
   message formatting and that setlogmask filters by severity. */
#include "rxdk_test.h"
#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

int main(void) {
    unlink("cache:/syslog.log");                 /* start from a clean log */

    openlog("t_syslog", LOG_PID, LOG_USER);
    syslog(LOG_INFO, "hello %d %s", 42, "world");
    syslog(LOG_ERR, "an error code %d", 7);

    int old = setlogmask(LOG_UPTO(LOG_WARNING)); /* drop DEBUG/INFO/NOTICE */
    CHECK(old != 0, "setlogmask returns the previous mask");
    syslog(LOG_DEBUG, "SHOULD_NOT_APPEAR debug line");
    setlogmask(old);
    closelog();

    int fd = open("cache:/syslog.log", O_RDONLY);
    CHECK(fd >= 0, "syslog.log created on the writable drive");
    char buf[1024];
    memset(buf, 0, sizeof buf);
    ssize_t r = 0;
    if (fd >= 0) { r = read(fd, buf, sizeof buf - 1); close(fd); }
    CHECK(r > 0, "syslog.log has content");
    CHECK(strstr(buf, "t_syslog[1]:") != NULL, "record carries ident[pid]");
    CHECK(strstr(buf, "hello 42 world") != NULL, "INFO message logged");
    CHECK(strstr(buf, "<INFO>") != NULL, "severity tag present");
    CHECK(strstr(buf, "an error code 7") != NULL, "ERR message logged");
    CHECK(strstr(buf, "SHOULD_NOT_APPEAR") == NULL, "setlogmask filtered the DEBUG line");

    unlink("cache:/syslog.log");
    CHECK_DONE("syslog");
    return 0;
}
