/* Batch I stragglers: renameat, sendfile, copy_file_range, posix_fadvise/
   fallocate, fpathconf, the (synchronous) POSIX aio family, and pthread
   cooperative cancellation. */
#include "rxdk_test.h"
#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <aio.h>
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <errno.h>

extern int nanosleep(const struct timespec *, struct timespec *);
static void nap(void) { struct timespec t = {0, 5 * 1000000L}; nanosleep(&t, 0); }

static volatile int g_cw_up;
static void *cancel_worker(void *a) {
    (void)a;
    g_cw_up = 1;
    for (;;) {
        pthread_testcancel();
        nap();
    }
    return (void *)(long)7;   /* never reached */
}

int main(void) {
    struct stat st;
    int fd;

    /* ---- renameat ---- */
    fd = open("cache:/p5src", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd >= 0) { write(fd, "hello", 5); close(fd); }
    unlink("cache:/p5dst");
    CHECK_EQI(renameat(AT_FDCWD, "cache:/p5src", AT_FDCWD, "cache:/p5dst"), 0, "renameat AT_FDCWD");
    CHECK(stat("cache:/p5dst", &st) == 0 && st.st_size == 5, "renameat moved the file");

    /* ---- sendfile ---- */
    int in = open("cache:/p5dst", O_RDONLY);
    int out = open("cache:/p5copy", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    off_t off = 0;
    CHECK_EQI((long)sendfile(out, in, &off, 5), 5, "sendfile copied 5 bytes");
    CHECK((long)off == 5, "sendfile advanced the offset");
    close(in); close(out);
    CHECK(stat("cache:/p5copy", &st) == 0 && st.st_size == 5, "sendfile output size");

    /* ---- copy_file_range ---- */
    int in2 = open("cache:/p5dst", O_RDONLY);
    int out2 = open("cache:/p5cfr", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    CHECK_EQI((long)copy_file_range(in2, 0, out2, 0, 5, 0), 5, "copy_file_range copied 5 bytes");
    close(in2); close(out2);

    /* ---- posix_fadvise / posix_fallocate ---- */
    fd = open("cache:/p5fa", O_CREAT | O_RDWR | O_TRUNC, 0644);
    CHECK_EQI(posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL), 0, "posix_fadvise -> 0");
    CHECK_EQI(posix_fallocate(fd, 0, 1000), 0, "posix_fallocate -> 0");
    CHECK(fstat(fd, &st) == 0 && st.st_size >= 1000, "posix_fallocate grew the file");
    close(fd);

    /* ---- fpathconf ---- */
    CHECK(fpathconf(0, _PC_NAME_MAX) == 255, "fpathconf _PC_NAME_MAX");
    CHECK(fpathconf(0, _PC_LINK_MAX) == 1, "fpathconf _PC_LINK_MAX (no hard links)");

    /* ---- POSIX aio (synchronous) ---- */
    int af = open("cache:/p5aio", O_CREAT | O_RDWR | O_TRUNC, 0644);
    struct aiocb wcb;
    memset(&wcb, 0, sizeof wcb);
    wcb.aio_fildes = af; wcb.aio_buf = (void *)"AIO!"; wcb.aio_nbytes = 4; wcb.aio_offset = 0;
    CHECK_EQI(aio_write(&wcb), 0, "aio_write submitted");
    CHECK_EQI(aio_error(&wcb), 0, "aio_error == 0 (already complete)");
    CHECK_EQI((long)aio_return(&wcb), 4, "aio_return == 4 written");

    char rb[8] = {0};
    struct aiocb rcb;
    memset(&rcb, 0, sizeof rcb);
    rcb.aio_fildes = af; rcb.aio_buf = rb; rcb.aio_nbytes = 4; rcb.aio_offset = 0;
    CHECK_EQI(aio_read(&rcb), 0, "aio_read submitted");
    CHECK_EQI((long)aio_return(&rcb), 4, "aio_return == 4 read");
    CHECK(memcmp(rb, "AIO!", 4) == 0, "aio round-trip data matches");
    CHECK_EQI(aio_fsync(0, &wcb), 0, "aio_fsync -> 0");
    CHECK_EQI(aio_cancel(af, &wcb), AIO_ALLDONE, "aio_cancel -> AIO_ALLDONE");
    const struct aiocb *list[1] = {&wcb};
    CHECK_EQI(aio_suspend(list, 1, 0), 0, "aio_suspend -> 0 (nothing pending)");
    close(af);

    /* ---- pthread cooperative cancellation ---- */
    int oldstate;
    CHECK_EQI(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate), 0, "pthread_setcancelstate");
    g_cw_up = 0;
    pthread_t ct;
    void *cres = 0;
    pthread_create(&ct, 0, cancel_worker, 0);
    while (!g_cw_up) nap();                 /* wait until the worker is looping */
    CHECK_EQI(pthread_cancel(ct), 0, "pthread_cancel");
    pthread_join(ct, &cres);
    CHECK(cres == PTHREAD_CANCELED, "cancelled thread returns PTHREAD_CANCELED");

    /* tidy up */
    unlink("cache:/p5dst"); unlink("cache:/p5copy"); unlink("cache:/p5cfr");
    unlink("cache:/p5fa"); unlink("cache:/p5aio");

    CHECK_DONE("posix5");
    return 0;
}
