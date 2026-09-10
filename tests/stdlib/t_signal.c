/* Cooperative signals + ITIMER_REAL: signal/sigaction register handlers, raise
   and kill(self) run them synchronously, and setitimer/alarm fire the SIGALRM
   handler from a helper thread. (No async pre-emption -- see signals.c.) */
#include "rxdk_test.h"
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

static volatile int g_got;
static void handler(int s) { g_got = s; }

extern int nanosleep(const struct timespec *, struct timespec *);
static void nap(long ms) { struct timespec t = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&t, 0); }

int main(void) {
    /* signal() registers and returns the previous disposition */
    _sig_func_ptr prev = signal(SIGUSR1, handler);
    CHECK(prev != SIG_ERR, "signal registers (not SIG_ERR)");
    g_got = 0;
    raise(SIGUSR1);
    CHECK_EQI(g_got, SIGUSR1, "raise invokes the handler");

    _sig_func_ptr p2 = signal(SIGUSR1, SIG_IGN);
    CHECK(p2 == handler, "signal returns the previous handler");
    g_got = 0;
    raise(SIGUSR1);
    CHECK_EQI(g_got, 0, "SIG_IGN suppresses the handler");

    /* sigaction */
    struct sigaction sa, old;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = handler;
    CHECK_EQI(sigaction(SIGUSR2, &sa, &old), 0, "sigaction installs");
    g_got = 0;
    raise(SIGUSR2);
    CHECK_EQI(g_got, SIGUSR2, "sigaction handler runs via raise");

    /* kill(self) */
    signal(SIGUSR1, handler);
    g_got = 0;
    CHECK_EQI(kill(getpid(), SIGUSR1), 0, "kill(self) -> 0");
    CHECK_EQI(g_got, SIGUSR1, "kill(self) delivers to the handler");

    /* setitimer(ITIMER_REAL): a 30ms one-shot fires the SIGALRM handler */
    signal(SIGALRM, handler);
    g_got = 0;
    struct itimerval it;
    memset(&it, 0, sizeof it);
    it.it_value.tv_usec = 30 * 1000;
    CHECK_EQI(setitimer(ITIMER_REAL, &it, 0), 0, "setitimer one-shot");
    nap(150);
    CHECK_EQI(g_got, SIGALRM, "setitimer fired the SIGALRM handler");
    struct itimerval cur;
    getitimer(ITIMER_REAL, &cur);
    CHECK(cur.it_value.tv_sec == 0 && cur.it_value.tv_usec == 0, "getitimer shows disarmed");

    /* alarm(): arm 5s, then cancel and read the remaining time */
    signal(SIGALRM, handler);
    CHECK_EQI((int)alarm(5), 0, "alarm returns 0 (none pending)");
    unsigned rem = alarm(0);
    CHECK(rem >= 4 && rem <= 5, "alarm(0) cancels and returns ~5s remaining");

    CHECK_DONE("signal");
    return 0;
}
