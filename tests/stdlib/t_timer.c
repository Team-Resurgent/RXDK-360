/* POSIX per-process timers over a helper thread (no signals needed for
   SIGEV_THREAD / SIGEV_NONE). SIGEV_SIGNAL is unsupported (no signal delivery). */
#include "rxdk_test.h"
#include <time.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

extern int nanosleep(const struct timespec *, struct timespec *);
static void nap(long ms) { struct timespec t = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&t, 0); }

static volatile int g_fires;
static void on_fire(union sigval v) { (void)v; g_fires++; }

int main(void) {
    struct sigevent sev;
    struct itimerspec its, cur, zero;
    timer_t t;

    memset(&zero, 0, sizeof zero);

    /* SIGEV_SIGNAL cannot be delivered */
    memset(&sev, 0, sizeof sev);
    sev.sigev_notify = SIGEV_SIGNAL;
    errno = 0;
    CHECK(timer_create(CLOCK_MONOTONIC, &sev, &t) == -1 && errno == ENOTSUP,
          "timer_create SIGEV_SIGNAL -> ENOTSUP");

    /* SIGEV_THREAD one-shot: the callback runs ~30ms later */
    memset(&sev, 0, sizeof sev);
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = on_fire;
    CHECK_EQI(timer_create(CLOCK_MONOTONIC, &sev, &t), 0, "timer_create SIGEV_THREAD");
    g_fires = 0;
    memset(&its, 0, sizeof its);
    its.it_value.tv_nsec = 30 * 1000000L;
    CHECK_EQI(timer_settime(t, 0, &its, 0), 0, "timer_settime one-shot");
    nap(120);
    CHECK_EQI(g_fires, 1, "one-shot timer fired exactly once");
    timer_gettime(t, &cur);
    CHECK(cur.it_value.tv_sec == 0 && cur.it_value.tv_nsec == 0,
          "gettime shows disarmed after a one-shot");

    /* periodic: 20ms interval fires several times over ~130ms */
    g_fires = 0;
    memset(&its, 0, sizeof its);
    its.it_value.tv_nsec = 20 * 1000000L;
    its.it_interval.tv_nsec = 20 * 1000000L;
    CHECK_EQI(timer_settime(t, 0, &its, 0), 0, "timer_settime periodic");
    nap(130);
    timer_settime(t, 0, &zero, 0);          /* disarm */
    CHECK(g_fires >= 3, "periodic timer fired multiple times");
    CHECK_EQI(timer_delete(t), 0, "timer_delete");

    /* SIGEV_NONE: no callback, countdown visible via gettime */
    memset(&sev, 0, sizeof sev);
    sev.sigev_notify = SIGEV_NONE;
    timer_t tn;
    CHECK_EQI(timer_create(CLOCK_MONOTONIC, &sev, &tn), 0, "timer_create SIGEV_NONE");
    memset(&its, 0, sizeof its);
    its.it_value.tv_sec = 10;
    timer_settime(tn, 0, &its, 0);
    timer_gettime(tn, &cur);
    CHECK(cur.it_value.tv_sec >= 8 && cur.it_value.tv_sec <= 10,
          "SIGEV_NONE gettime shows the countdown");
    CHECK_EQI(timer_delete(tn), 0, "timer_delete (SIGEV_NONE)");

    CHECK_DONE("timer");
    return 0;
}
