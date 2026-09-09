#ifndef RXDK_LIBCPP_PREREQ_H
#define RXDK_LIBCPP_PREREQ_H

/* Force-included ahead of the libc++/libc++abi sources.
 *
 * This used to hand-declare the few POSIX symbols libc++ needs that picolibc
 * hides under strict -std (strerror_r for <system_error>, clock_gettime +
 * CLOCK_* for <chrono>). Now that the C++ build compiles with -D_GNU_SOURCE
 * (see build_libcpp.py) picolibc exposes the whole POSIX/GNU surface -- locale_t
 * and uselocale/newlocale for <locale>/<iostream> included -- so those manual
 * declarations are gone (and would conflict with picolibc's real prototypes).
 *
 * The header is kept as a (now empty) force-include so the build/test command
 * lines that reference it stay valid; add narrowly-scoped prerequisites here if
 * a future source needs one.
 */

#endif
