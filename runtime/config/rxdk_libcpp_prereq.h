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

#endif
