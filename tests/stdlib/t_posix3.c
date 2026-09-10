/* Batch F: clock_nanosleep (relative + absolute), telldir/seekdir, mkdtemp/
   mkostemp, and POSIX unnamed semaphores (real counting semaphores over the
   C11 mutex/condvar). */
#include "rxdk_test.h"
#include <sys/types.h>
#include <time.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <errno.h>

static void mk(const char *p) {
    int fd = creat(p, 0644);
    if (fd >= 0) { write(fd, "x", 1); close(fd); }
}

static long long elapsed_ns(struct timespec a, struct timespec b) {
    return (long long)(b.tv_sec - a.tv_sec) * 1000000000LL + (b.tv_nsec - a.tv_nsec);
}

int main(void) {
    struct timespec t0, t1, req, now, fut;

    /* seed the temp-name RNG from the clock so mkdtemp/mkostemp pick fresh names
       each run (random() is otherwise deterministic, and cache: is persistent). */
    clock_gettime(CLOCK_MONOTONIC, &t0);
    srandom((unsigned)t0.tv_nsec ^ (unsigned)t0.tv_sec);

    /* ---- clock_nanosleep ---- */
    clock_gettime(CLOCK_MONOTONIC, &t0);
    req.tv_sec = 0; req.tv_nsec = 30 * 1000000L;
    CHECK_EQI(clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL), 0, "clock_nanosleep relative -> 0");
    clock_gettime(CLOCK_MONOTONIC, &t1);
    CHECK(elapsed_ns(t0, t1) >= 20 * 1000000LL, "clock_nanosleep relative actually waited");

    struct timespec past = {0, 0};
    CHECK_EQI(clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &past, NULL), 0,
              "clock_nanosleep past deadline -> 0 immediately");

    clock_gettime(CLOCK_MONOTONIC, &now);
    fut = now; fut.tv_nsec += 30 * 1000000L;
    if (fut.tv_nsec >= 1000000000L) { fut.tv_nsec -= 1000000000L; fut.tv_sec++; }
    clock_gettime(CLOCK_MONOTONIC, &t0);
    CHECK_EQI(clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &fut, NULL), 0,
              "clock_nanosleep abstime -> 0");
    clock_gettime(CLOCK_MONOTONIC, &t1);
    CHECK(elapsed_ns(t0, t1) >= 20 * 1000000LL, "clock_nanosleep abstime waited");

    /* ---- telldir / seekdir ---- */
    mkdir("cache:/td", 0777);
    mk("cache:/td/a"); mk("cache:/td/b"); mk("cache:/td/c");
    DIR *d = opendir("cache:/td");
    CHECK(d != NULL, "opendir for telldir/seekdir");
    if (d) {
        char name[256];
        struct dirent *e;
        readdir(d);                       /* entry 0 */
        long p1 = telldir(d);             /* position of entry 1 */
        e = readdir(d);                   /* entry 1 */
        strcpy(name, e ? e->d_name : "");
        readdir(d);                       /* entry 2 */
        seekdir(d, p1);                   /* rewind to entry 1 */
        CHECK_EQI(telldir(d), p1, "telldir after seekdir matches");
        e = readdir(d);
        CHECK(e && strcmp(e->d_name, name) == 0, "seekdir returns to the same entry");
        closedir(d);
    }

    /* ---- mkdtemp ---- */
    char dt[] = "cache:/dtXXXXXX";
    struct stat st;
    CHECK(mkdtemp(dt) == dt, "mkdtemp returns the template");
    CHECK(strstr(dt, "XXXXXX") == NULL, "mkdtemp filled the Xs");
    CHECK(stat(dt, &st) == 0 && S_ISDIR(st.st_mode), "mkdtemp created a directory");

    /* ---- mkostemp ---- */
    char ft[] = "cache:/ftXXXXXX";
    int fd = mkostemp(ft, 0);
    CHECK(fd >= 0, "mkostemp returns an fd");
    CHECK(strstr(ft, "XXXXXX") == NULL, "mkostemp filled the Xs");
    if (fd >= 0) {
        write(fd, "hi", 2);
        close(fd);
        CHECK(stat(ft, &st) == 0 && st.st_size == 2, "mkostemp file is usable");
        unlink(ft);
    }
    rmdir(dt);   /* clean up so repeated runs don't accumulate temp entries */

    /* ---- POSIX unnamed semaphores ---- */
    sem_t s;
    int v;
    CHECK_EQI(sem_init(&s, 0, 2), 0, "sem_init(value=2)");
    sem_getvalue(&s, &v);
    CHECK_EQI(v, 2, "sem_getvalue == 2");
    CHECK_EQI(sem_wait(&s), 0, "sem_wait");
    CHECK_EQI(sem_trywait(&s), 0, "sem_trywait (drains to 0)");
    errno = 0;
    CHECK(sem_trywait(&s) == -1 && errno == EAGAIN, "sem_trywait on empty -> EAGAIN");
    CHECK_EQI(sem_post(&s), 0, "sem_post");
    sem_getvalue(&s, &v);
    CHECK_EQI(v, 1, "sem_getvalue == 1 after post");
    CHECK_EQI(sem_wait(&s), 0, "sem_wait drains the posted token");
    CHECK_EQI(sem_destroy(&s), 0, "sem_destroy");

    CHECK_DONE("posix3");
    return 0;
}
