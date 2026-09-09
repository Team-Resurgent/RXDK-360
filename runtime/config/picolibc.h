/* Generated for RXDK-Libs Xbox HAL (picolibc 1.8.x profile). */
#pragma once

#define __ATOMIC_UNGETC 1
#define __FAST_STRCMP 1
#define __PICOLIBC_ERRNO_FUNCTION __rxdk_errno
#define __TINY_STDIO 1
#define __HAVE_COMPLEX 1
#define __IO_C99_FORMATS 1
#define __IO_LONG_LONG 1
/* %ls/%lc in the narrow printf family, which is what MSVC's %S/%C are rewritten
   to (libs/libc/xbox/ms_printf.c). Deliberately not __MB_CAPABLE: without it
   picolibc narrows a wchar_t by truncation rather than through a locale's
   multibyte encoding, which is the behaviour Xbox paths and gamertags want. */
#define __IO_WCHAR 1
/* Default printf/scanf I/O variant = double ('d'): float formatting/parsing for
   printf, scanf, and iostream num_put/num_get. Backed by the Ryu engines
   (dtoa_ryu.c etc., compiled in libs/libc/build.zig). Was 0 (no float ->
   picolibc emitted the "*float*" placeholder for %f/%g). */
#define __IO_DEFAULT 'd'
#define __NEWLIB_VERSION "4.3.0"
#define _NEWLIB_VERSION "4.3.0"
#define _PICOLIBC_MINOR__ 8
#define _PICOLIBC_VERSION "1.8.11"
#define _PICOLIBC__ 1
#define __NEWLIB_MINOR__ 3
#define __NEWLIB_PATCHLEVEL__ 0
#define __NEWLIB__ 4
#define __PICOLIBC_MINOR__ 8
#define __PICOLIBC_PATCHLEVEL__ 11
#define __PICOLIBC_VERSION__ "1.8.11"
#define __PICOLIBC__ 1
#define ENABLE_PICOLIBC_EXIT 1
#define __HAVE_POSIX_LOCALE_API 1
#define __OBSOLETE_MATH_FLOAT 0
#define __OBSOLETE_MATH_DOUBLE 0
