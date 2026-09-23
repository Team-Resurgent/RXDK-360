/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * sysconf() for the Xbox 360. picolibc declares it but ships no implementation
 * for a bare target -- so libc++'s std::thread::hardware_concurrency() (which
 * calls sysconf(_SC_NPROCESSORS_ONLN)) otherwise compiles to a hard "return 0".
 * The Xenon CPU is fixed at 3 cores x 2 hardware threads = 6, so we report that.
 *
 * The consolidated picolibc 'xbox' branch now defines _SC_NPROCESSORS_ONLN /
 * _SC_NPROCESSORS_CONF (sys/unistd.h) itself, so we switch on picolibc's own
 * values via <unistd.h> -- no RXDK-private numbering, and nothing to keep in sync
 * with the libc++ build (the earlier -D_SC_NPROCESSORS_ONLN override is dropped).
 */
#include <unistd.h>

long sysconf(int name)
{
    switch (name) {
#ifdef _SC_NPROCESSORS_ONLN
    case _SC_NPROCESSORS_ONLN:
#endif
#ifdef _SC_NPROCESSORS_CONF
    case _SC_NPROCESSORS_CONF:
#endif
        return 6;    /* Xenon: 3 cores x 2 hardware threads */
    default:
        return -1;   /* unknown/unsupported name (POSIX: errno = EINVAL) */
    }
}
