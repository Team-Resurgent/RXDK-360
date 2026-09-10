/* POSIX layer additions: dup/dup2, pread/pwrite, creat, fsync, fcntl, access,
   isatty, the sleep family, posix_memalign, basename/dirname. File ops use the
   writable cache: drive. */
#include "rxdk_test.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define PATH "cache:/posix_test.bin"
#define PW   "cache:/posix_pw.bin"

int main(void) {
    /* --- set up a known 10-byte file --- */
    int wf = creat(PATH, 0644);
    CHECK(wf >= 0, "creat() opens a new file");
    CHECK(write(wf, "0123456789", 10) == 10, "write the fixture");
    CHECK(fsync(wf) == 0, "fsync flushes");
    close(wf);

    /* --- access --- */
    CHECK(access(PATH, 0) == 0, "access() existing file");
    CHECK(access("cache:/no_such_file.bin", 0) == -1, "access() missing file -> -1");

    /* --- dup shares the open-file description (offset) --- */
    {
        int fd = open(PATH, O_RDONLY);
        char a[4], b[4], c[2];
        CHECK(read(fd, a, 4) == 4, "read first 4");
        int fd2 = dup(fd);
        CHECK(fd2 >= 0 && fd2 != fd, "dup returns a new fd");
        CHECK(read(fd2, b, 4) == 4, "read via dup");
        CHECK(memcmp(a, "0123", 4) == 0 && memcmp(b, "4567", 4) == 0,
              "dup shares the file offset");
        close(fd2);
        CHECK(read(fd, c, 2) == 2 && memcmp(c, "89", 2) == 0,
              "original fd still valid after dup closed");
        close(fd);
    }

    /* --- dup2 onto a chosen fd --- */
    {
        int fd = open(PATH, O_RDONLY);
        int r  = dup2(fd, 20);
        CHECK(r == 20, "dup2 targets the requested fd");
        char x[3];
        CHECK(read(20, x, 3) == 3 && memcmp(x, "012", 3) == 0, "read via dup2 target");
        close(20);
        close(fd);
    }

    /* --- pread does not disturb the fd offset --- */
    {
        int fd = open(PATH, O_RDONLY);
        char junk[3], p[4];
        read(fd, junk, 3);                       /* offset -> 3 */
        CHECK(pread(fd, p, 4, 0) == 4 && memcmp(p, "0123", 4) == 0, "pread reads at offset");
        CHECK(lseek(fd, 0, SEEK_CUR) == 3, "pread left the fd offset alone");
        close(fd);
    }

    /* --- pwrite at an offset --- */
    {
        int fd = creat(PW, 0644);
        write(fd, "AAAAAA", 6);
        CHECK(pwrite(fd, "XY", 2, 2) == 2, "pwrite writes 2 bytes");
        close(fd);
        int rf = open(PW, O_RDONLY);
        char rb[6];
        read(rf, rb, 6);
        close(rf);
        CHECK(memcmp(rb, "AAXYAA", 6) == 0, "pwrite landed at offset 2");
    }

    /* --- fcntl --- */
    {
        int fd = open(PATH, O_RDONLY);
        int df = fcntl(fd, F_DUPFD, 0);
        CHECK(df >= 0, "fcntl(F_DUPFD) dups");
        CHECK(fcntl(fd, F_GETFL) >= 0, "fcntl(F_GETFL)");
        close(df);
        close(fd);
    }

    /* --- isatty --- */
    {
        CHECK(isatty(1) == 1, "isatty(stdout) is a tty");
        int fd = open(PATH, O_RDONLY);
        CHECK(isatty(fd) == 0, "isatty(file) is not a tty");
        close(fd);
    }

    /* --- sleep family: measure against the monotonic clock --- */
    {
        struct timespec t0, t1, req;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        req.tv_sec = 0; req.tv_nsec = 30 * 1000 * 1000;   /* 30 ms */
        CHECK(nanosleep(&req, (struct timespec *)0) == 0, "nanosleep returns 0");
        clock_gettime(CLOCK_MONOTONIC, &t1);
        long ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
        CHECK(ms >= 25, "nanosleep waited ~30ms");

        clock_gettime(CLOCK_MONOTONIC, &t0);
        CHECK(usleep(20000) == 0, "usleep returns 0");     /* 20 ms */
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
        CHECK(ms >= 15, "usleep waited ~20ms");
        CHECK(sleep(0) == 0, "sleep(0) returns immediately");
    }

    /* --- posix_memalign --- */
    {
        void *p = (void *)0;
        int r   = posix_memalign(&p, 64, 100);
        CHECK(r == 0 && p != (void *)0, "posix_memalign succeeds");
        CHECK(((uintptr_t)p & 63u) == 0, "posix_memalign is 64-aligned");
        free(p);
        void *q = (void *)0;
        CHECK(posix_memalign(&q, 3, 16) != 0, "posix_memalign rejects non-power-of-two");
    }

    /* cleanup */
    unlink(PATH);
    unlink(PW);

    CHECK_DONE("posix");
    return 0;
}
