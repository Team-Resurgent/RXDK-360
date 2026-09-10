/* POSIX functions the console has no concept of: process/signal/mmap/tty/timer/
   permission stubs. They exist so portable code links; this checks each returns
   its documented value. (raise(SIGABRT) and _exit are not exercised -- they end
   the program.) */
#include "rxdk_test.h"
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <termios.h>
#include <errno.h>
#include <stdlib.h>

int main(void) {
    /* identity */
    CHECK_EQI(getpid(), 1, "getpid == 1 (the single title)");
    CHECK_EQI(getppid(), 0, "getppid == 0");
    CHECK_EQI((long)getuid(), 0, "getuid == 0");

    /* no process model -> ENOSYS/errors, but linkable */
    errno = 0;
    CHECK(fork() == -1 && errno == ENOSYS, "fork -> ENOSYS");
    errno = 0;
    CHECK(kill(999, 0) == -1, "kill(other pid) -> error (no such process)");
    CHECK(pause() == -1, "pause returns (does not hang)");
    CHECK_EQI((long)alarm(0), 0, "alarm(0) -> 0 (nothing pending)");

    /* signal registration is real (cooperative) -- returns the previous
       disposition, not SIG_ERR (full behaviour is exercised in t_signal) */
    CHECK(signal(SIGINT, SIG_DFL) != SIG_ERR, "signal installs (returns previous)");
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    CHECK(sigismember(&set, SIGINT) == 1, "sigaddset/sigismember");
    CHECK(sigismember(&set, SIGTERM) == 0, "sigismember absent");
    sigset_t old;
    CHECK(sigprocmask(SIG_BLOCK, &set, &old) == 0, "sigprocmask -> 0");
    CHECK(raise(SIGUSR1) == 0, "raise(non-abort) -> 0 (ignored)");

    /* virtual memory */
    CHECK(mmap((void *)0, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED,
          "mmap -> MAP_FAILED");
    CHECK(munmap((void *)0x1000, 4096) == -1, "munmap -> -1");
    int local = 0;
    CHECK(mprotect(&local, sizeof local, PROT_READ | PROT_WRITE) == 0,
          "mprotect succeeds (memory already accessible)");
    errno = 0;
    CHECK(sbrk(0) == (void *)-1, "sbrk -> (void*)-1 (no break)");

    /* terminal */
    struct termios t;
    errno = 0;
    CHECK(tcgetattr(1, &t) == -1 && errno == ENOTTY, "tcgetattr -> ENOTTY");

    /* interval timers are real for ITIMER_REAL (exercised in t_signal); a
       fresh getitimer reports a disarmed timer */
    struct itimerval it;
    CHECK(getitimer(ITIMER_REAL, &it) == 0 && it.it_value.tv_sec == 0 && it.it_value.tv_usec == 0,
          "getitimer -> 0, disarmed");

    /* permissions: no-op success on the permission-less volumes */
    CHECK(chmod("cache:/whatever", 0644) == 0, "chmod -> 0 (no-op)");
    CHECK(chown("cache:/whatever", 0, 0) == 0, "chown -> 0 (no-op)");
    CHECK_EQI((long)umask(022), 0, "umask -> 0");

    CHECK_DONE("posixstub");
    return 0;
}
