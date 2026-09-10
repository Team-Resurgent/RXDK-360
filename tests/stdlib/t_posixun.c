/* The "unsupported" POSIX set: facilities a single-title console cannot provide
   (spawn/pipe/pty/IPC/timers/scheduling/identity). These are linkable stubs --
   this section just proves they link and return their documented failure or the
   console's fixed value. */
#define _POSIX_PRIORITY_SCHEDULING 1   /* expose <sched.h> policy declarations */
#include "rxdk_test.h"
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sched.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <stdlib.h>
#include <semaphore.h>
#include <mqueue.h>
#include <iconv.h>

int main(void) {
    /* spawn / pipes / fifo */
    int p[2];
    errno = 0; CHECK(pipe(p) == -1 && errno == ENOSYS, "pipe -> ENOSYS");
    errno = 0; CHECK(mkfifo("cache:/nope", 0644) == -1 && errno == ENOSYS, "mkfifo -> ENOSYS");
    errno = 0; CHECK(vfork() == -1 && errno == ENOSYS, "vfork -> ENOSYS");
    CHECK_EQI(posix_spawn(0, "x", 0, 0, 0, 0), ENOSYS, "posix_spawn -> ENOSYS");

    /* IPC: shared memory, message queues, named semaphores */
    errno = 0; CHECK(shm_open("/s", 0, 0) == -1 && errno == ENOSYS, "shm_open -> ENOSYS");
    CHECK(mq_open("/q", 0) == (mqd_t)-1, "mq_open -> -1");
    CHECK(sem_open("/n", 0) == SEM_FAILED, "sem_open -> SEM_FAILED");

    /* pseudo-terminals */
    errno = 0; CHECK(posix_openpt(0) == -1 && errno == ENOSYS, "posix_openpt -> ENOSYS");
    CHECK(ptsname(0) == NULL, "ptsname -> NULL");

    /* per-process timers */
    timer_t t;
    errno = 0; CHECK(timer_create(CLOCK_REALTIME, 0, &t) == -1 && errno == ENOSYS, "timer_create -> ENOSYS");

    /* process groups / sessions (one title) */
    CHECK((long)getpgrp() == 1, "getpgrp == 1");
    CHECK((long)setsid() == 1, "setsid == 1");
    CHECK_EQI(setpgid(0, 0), 0, "setpgid -> 0");
    CHECK((long)tcgetpgrp(0) == 1, "tcgetpgrp == 1");

    /* scheduling policy (one class) */
    CHECK_EQI(sched_get_priority_max(0), 0, "sched_get_priority_max -> 0");
    CHECK_EQI(sched_getscheduler(0), 0, "sched_getscheduler -> SCHED_OTHER");

    /* identity (single fixed user) */
    CHECK_EQI(setuid(0), 0, "setuid(0) -> 0");
    CHECK_EQI(nice(5), 0, "nice -> 0");

    /* resource usage: zeroed */
    struct rusage ru;
    ru.ru_utime.tv_sec = 12345;
    CHECK_EQI(getrusage(RUSAGE_SELF, &ru), 0, "getrusage -> 0");
    CHECK(ru.ru_utime.tv_sec == 0, "getrusage zeroed the struct");

    /* wait: no children */
    int st;
    errno = 0; CHECK(wait4(-1, &st, 0, 0) == -1 && errno == ECHILD, "wait4 -> ECHILD");

    /* virtual memory: already resident */
    CHECK_EQI(mlockall(0), 0, "mlockall -> 0 (memory resident)");
    CHECK_EQI(msync(0, 0, 0), 0, "msync -> 0");
    CHECK_EQI(syncfs(0), 0, "syncfs -> 0");

    /* extended signals */
    sigset_t set;
    CHECK_EQI(sigpending(&set), 0, "sigpending -> 0 (nothing pending)");
    errno = 0; CHECK(killpg(1, 0) == -1 && errno == EPERM, "killpg -> EPERM");

    /* iconv: clean-failing stub (functional iconv needs __MB_CAPABLE) */
    CHECK(iconv_open("UTF-8", "UTF-8") == (iconv_t)-1, "iconv_open -> (iconv_t)-1 (detectable failure)");

    CHECK_DONE("posixun");
    return 0;
}
