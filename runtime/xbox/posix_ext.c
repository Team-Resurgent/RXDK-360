/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * Small self-contained POSIX functions the modern runtime was missing -- the
 * ones that ride on primitives we already have (the C11 thread sleep, our
 * aligned allocator, stat()) or are pure string ops. The OS-layer file/fd
 * additions (dup/pread/...) live in fileio.c next to the fd table; the
 * process/signal/mmap stubs live in posix_stubs.c.
 */
#define _GNU_SOURCE 1
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

/* ---- sleep family: all wait via the C11 thread sleep (threads.c), which does
   a relative kernel wait rounded up to whole ms. There are no POSIX signals, so
   a sleep always runs to completion (no early EINTR). ------------------------ */
extern int thrd_sleep(const struct timespec *__duration, struct timespec *__remaining);

int nanosleep(const struct timespec *__req, struct timespec *__rem)
{
    if (!__req || __req->tv_sec < 0 || __req->tv_nsec < 0 || __req->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
    }
    thrd_sleep(__req, __rem);
    return 0;
}

unsigned sleep(unsigned __seconds)
{
    struct timespec __ts;
    __ts.tv_sec  = (time_t)__seconds;
    __ts.tv_nsec = 0;
    nanosleep(&__ts, (struct timespec *)0);
    return 0; /* nothing interrupts us -> full time slept */
}

int usleep(useconds_t __usec)
{
    struct timespec __ts;
    __ts.tv_sec  = (time_t)(__usec / 1000000u);
    __ts.tv_nsec = (long)(__usec % 1000000u) * 1000L;
    return nanosleep(&__ts, (struct timespec *)0);
}

/* ---- posix_memalign over our aligned allocator (rt_support.c). C11
   aligned_alloc wants size to be a multiple of the alignment, which POSIX does
   not require, so round the request up. ------------------------------------- */
extern void *aligned_alloc(size_t __alignment, size_t __size);

int posix_memalign(void **__memptr, size_t __alignment, size_t __size)
{
    if (!__memptr)
        return EINVAL;
    if (__alignment < sizeof(void *) || (__alignment & (__alignment - 1)) != 0)
        return EINVAL; /* must be a power of two and a multiple of sizeof(void*) */
    size_t __rounded = (__size + __alignment - 1) & ~(__alignment - 1);
    if (__rounded == 0)
        __rounded = __alignment;
    void *__p = aligned_alloc(__alignment, __rounded);
    if (!__p)
        return ENOMEM;
    *__memptr = __p;
    return 0;
}

/* ---- isatty: fds 0/1/2 are the console debug channel; nothing else is a tty. */
int isatty(int __fd)
{
    if (__fd >= 0 && __fd <= 2)
        return 1;
    errno = ENOTTY;
    return 0;
}

/* ---- access: FATX/GDFX have no per-file permission bits, so an existing file
   is treated as accessible for any requested mode; only existence is real. --- */
int access(const char *__path, int __amode)
{
    struct stat __st;
    (void)__amode;
    if (stat(__path, &__st) != 0)
        return -1; /* errno set by stat */
    return 0;
}

/* ---- mkdtemp / mkostemp: the temp-name makers picolibc's mktemp.c omits
   (it ships mkstemp/mkstemps/mkostemps but not these two). Both fill the six
   trailing 'X's with random [a-z0-9] and retry on collision -- mkdtemp creates
   a directory, mkostemp an O_EXCL file with caller flags. ---------------------- */
extern long random(void);

static int rxdk_fill_template(char *tmpl) {
    static const char cset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    size_t len = strlen(tmpl);
    char *x;
    int i;
    if (len < 6) { errno = EINVAL; return -1; }
    x = tmpl + len - 6;
    for (i = 0; i < 6; i++)
        if (x[i] != 'X') { errno = EINVAL; return -1; }
    for (i = 0; i < 6; i++)
        x[i] = cset[(unsigned long)random() % 36];
    return 0;
}

char *mkdtemp(char *tmpl) {
    int attempt;
    if (rxdk_fill_template(tmpl) != 0)
        return NULL;
    for (attempt = 0; attempt < 128; attempt++) {
        if (mkdir(tmpl, 0700) == 0)
            return tmpl;
        if (errno != EEXIST)
            return NULL;
        if (rxdk_fill_template(tmpl) != 0)
            return NULL;
    }
    errno = EEXIST;
    return NULL;
}

int mkostemp(char *tmpl, int flags) {
    int attempt, fd;
    if (rxdk_fill_template(tmpl) != 0)
        return -1;
    for (attempt = 0; attempt < 128; attempt++) {
        fd = open(tmpl, O_CREAT | O_EXCL | O_RDWR | flags, 0600);
        if (fd >= 0)
            return fd;
        if (errno != EEXIST)
            return -1;
        if (rxdk_fill_template(tmpl) != 0)
            return -1;
    }
    errno = EEXIST;
    return -1;
}

/* ---- sched_yield: hand the core to another ready thread (C11 thrd_yield) --- */
extern void thrd_yield(void);
int sched_yield(void) { thrd_yield(); return 0; }

/* ---- getentropy / getrandom: the console has no hardware entropy source, and
   picolibc's arc4random SEEDS itself from getentropy(), so this must be a
   self-contained generator (using arc4random here would recurse). It is a
   clock-seeded xorshift PRNG -- fine for tempfile names/hashing/dedup, NOT for
   cryptographic keys. getentropy is the seed source arc4random then builds on. */
static unsigned g_ent_state;

static unsigned ent_next(void) {
    unsigned x = g_ent_state;
    if (x == 0) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        x = (unsigned)ts.tv_nsec ^ ((unsigned)ts.tv_sec << 16) ^ 0x9E3779B9u;
        if (x == 0) x = 0xA5A5A5A5u;
    }
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;   /* xorshift32 */
    g_ent_state = x;
    return x;
}

int getentropy(void *buf, size_t len) {
    unsigned char *p = (unsigned char *)buf;
    if (len > 256) { errno = EIO; return -1; }
    if (!buf && len) { errno = EFAULT; return -1; }
    while (len) {
        unsigned r = ent_next();
        size_t n = len < 4 ? len : 4, i;
        for (i = 0; i < n; i++) { p[i] = (unsigned char)(r & 0xff); r >>= 8; }
        p += n; len -= n;
    }
    return 0;
}

ssize_t getrandom(void *buf, size_t len, unsigned int flags) {
    unsigned char *p = (unsigned char *)buf;
    size_t left = len;
    (void)flags;
    if (!buf && len) { errno = EFAULT; return -1; }
    while (left) {                             /* getentropy caps at 256/call */
        size_t chunk = left < 256 ? left : 256;
        getentropy(p, chunk);
        p += chunk; left -= chunk;
    }
    return (ssize_t)len;
}

/* ---- confstr: the console has one fixed environment; report the launch drive
   for _CS_PATH and empty strings for the rest, returning the full length. ----- */
size_t confstr(int name, char *buf, size_t len) {
    const char *val = (name == _CS_PATH) ? "game:\\" : "";
    size_t n = strlen(val);
    if (buf && len) {
        size_t c = (n < len - 1) ? n : len - 1;
        memcpy(buf, val, c);
        buf[c] = '\0';
    }
    return n + 1;
}
