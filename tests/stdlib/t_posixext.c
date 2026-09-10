/* Second POSIX-extensions batch: poll/ppoll (fds always ready), scandir/
   alphasort over the writable cache: drive, XPG basename/dirname, sync(), and
   the ffs bit-scan family. */
#include "rxdk_test.h"
#include <sys/types.h>
#include <poll.h>
#include <signal.h>
#include <dirent.h>
#include <string.h>
#include <libgen.h>
#include <strings.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

extern int ppoll(struct pollfd *, nfds_t, const struct timespec *, const sigset_t *);

static void mk(const char *p) {
    int fd = creat(p, 0644);
    if (fd >= 0) { write(fd, "x", 1); close(fd); }
}

static int only_txt(const struct dirent *e) {
    size_t l = strlen(e->d_name);
    return l >= 4 && strcmp(e->d_name + l - 4, ".txt") == 0;
}

int main(void) {
    /* ---- ffs / ffsl / ffsll ---- */
    CHECK_EQI(ffs(0), 0, "ffs(0) == 0");
    CHECK_EQI(ffs(1), 1, "ffs(1) == 1");
    CHECK_EQI(ffs(0x10), 5, "ffs(0x10) == 5");
    CHECK_EQI(ffs((int)0x80000000u), 32, "ffs(top bit) == 32");
    CHECK_EQI(ffsl(0x100L), 9, "ffsl(0x100) == 9");
    CHECK_EQI(ffsll(0x1ULL << 40), 41, "ffsll(1<<40) == 41");

    /* ---- basename / dirname (XPG: modify a writable copy) ---- */
    char b1[] = "/usr/lib/file.txt"; CHECK_STR(basename(b1), "file.txt", "basename: last component");
    char b2[] = "/usr/lib/";         CHECK_STR(basename(b2), "lib",      "basename: trailing slash");
    char b3[] = "/";                 CHECK_STR(basename(b3), "/",        "basename: root");
    char b4[] = "noslash";           CHECK_STR(basename(b4), "noslash",  "basename: no slash");
    char d1[] = "/usr/lib/file.txt"; CHECK_STR(dirname(d1),  "/usr/lib", "dirname: parent");
    char d2[] = "/usr/lib/";         CHECK_STR(dirname(d2),  "/usr",     "dirname: trailing slash");
    char d3[] = "file";              CHECK_STR(dirname(d3),  ".",        "dirname: no slash -> .");
    char d4[] = "/";                 CHECK_STR(dirname(d4),  "/",        "dirname: root");

    /* ---- poll / ppoll ---- */
    struct pollfd pf[2];
    pf[0].fd = 0; pf[0].events = POLLIN;  pf[0].revents = 0;
    pf[1].fd = 2; pf[1].events = POLLOUT; pf[1].revents = 0;
    CHECK_EQI(poll(pf, 2, 0), 2, "poll: 2 fds ready");
    CHECK(pf[0].revents & POLLIN, "poll: POLLIN reported");
    CHECK(pf[1].revents & POLLOUT, "poll: POLLOUT reported");
    struct pollfd pn; pn.fd = -1; pn.events = POLLIN; pn.revents = 0xF;
    CHECK_EQI(poll(&pn, 1, 0), 0, "poll: negative fd not counted");
    CHECK_EQI(pn.revents, 0, "poll: negative fd revents cleared");
    struct pollfd pp; pp.fd = 1; pp.events = POLLOUT; pp.revents = 0;
    CHECK_EQI(ppoll(&pp, 1, NULL, NULL), 1, "ppoll: fd ready");

    /* ---- scandir / alphasort ---- */
    mkdir("cache:/sdtest", 0777);
    mk("cache:/sdtest/b.txt");
    mk("cache:/sdtest/a.txt");
    mk("cache:/sdtest/c.txt");
    struct dirent **nl = NULL;
    int n = scandir("cache:/sdtest", &nl, only_txt, alphasort);
    CHECK_EQI(n, 3, "scandir: 3 filtered .txt entries");
    if (n == 3) {
        CHECK_STR(nl[0]->d_name, "a.txt", "scandir: alphasort [0]");
        CHECK_STR(nl[1]->d_name, "b.txt", "scandir: alphasort [1]");
        CHECK_STR(nl[2]->d_name, "c.txt", "scandir: alphasort [2]");
    }
    for (int i = 0; i < n; i++) free(nl[i]);
    free(nl);

    /* ---- sync ---- */
    sync();
    CHECK(1, "sync() returns");

    CHECK_DONE("posixext");
    return 0;
}
