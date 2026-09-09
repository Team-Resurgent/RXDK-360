#ifndef RXDK_LIBCPP_PREREQ_H
#define RXDK_LIBCPP_PREREQ_H

/* Force-included ahead of the libc++/libc++abi sources. picolibc only declares
   strerror_r under _POSIX_C_SOURCE >= 200112 / _GNU_SOURCE, which would widen
   header visibility across the whole build; instead declare just the one symbol
   <system_error> needs. picolibc provides the GNU (char*) variant. */
#ifdef __cplusplus
extern "C" {
#endif
char *strerror_r(int, char *, unsigned long);
#ifdef __cplusplus
}
#endif

/* <chrono> uses clock_gettime + CLOCK_MONOTONIC/CLOCK_REALTIME (our runtime
   implements clock_gettime in runtime/xbox/clock.c). picolibc gates those on
   POSIX/GNU visibility, off under strict -std=c++23, so declare the minimum.
   struct timespec is still visible from <time.h>; forward-declare it here since
   this header is force-included ahead of it (a pointer decl needs no definition). */
#ifndef CLOCK_REALTIME
#  define CLOCK_REALTIME 1
#endif
#ifndef CLOCK_MONOTONIC
#  define CLOCK_MONOTONIC 4
#endif
struct timespec;
#ifdef __cplusplus
extern "C" {
#endif
int clock_gettime(int, struct timespec *);
#ifdef __cplusplus
}
#endif

#endif
