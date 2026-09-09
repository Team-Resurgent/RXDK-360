/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * sysconf() for the Xbox 360. picolibc declares it but ships no implementation
 * for a bare target, and it does not define the glibc extension
 * _SC_NPROCESSORS_ONLN at all -- so libc++'s std::thread::hardware_concurrency()
 * (which calls sysconf(_SC_NPROCESSORS_ONLN)) otherwise compiles to a hard
 * "return 0". The Xenon CPU is fixed at 3 cores x 2 hardware threads = 6, so we
 * report that. The _SC_NPROCESSORS_* numbers are ours (picolibc's own _SC_
 * values stop at 121); the libc++ build is told the same value via
 * -D_SC_NPROCESSORS_ONLN (build_libcpp.py) so the two agree.
 */

#define RXDK_SC_NPROCESSORS_ONLN 200
#define RXDK_SC_NPROCESSORS_CONF 201

long sysconf(int name)
{
    switch (name) {
    case RXDK_SC_NPROCESSORS_ONLN:
    case RXDK_SC_NPROCESSORS_CONF:
        return 6;    /* Xenon: 3 cores x 2 hardware threads */
    default:
        return -1;   /* unknown/unsupported name (POSIX: errno = EINVAL) */
    }
}
