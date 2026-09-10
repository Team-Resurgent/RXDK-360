/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * Pure string/path helpers picolibc leaves to the platform:
 *
 *   - basename()/dirname() in their POSIX/XPG form (from <libgen.h>): they take
 *     a modifiable char* and split a path on '/'. This translation unit is
 *     deliberately compiled WITHOUT _GNU_SOURCE so <string.h> does not expose
 *     the conflicting GNU basename(const char*); <libgen.h> then aliases the
 *     XPG basename to the exported "basename" symbol.
 *   - ffs(): the 1-based index of the least-significant set bit (0 for zero),
 *     written with plain shifts so -fno-builtin never turns it into a
 *     self-referential __builtin_ffs. (picolibc already ships ffsl/ffsll.)
 */
#include <string.h>
#include <libgen.h>
#include <strings.h>

/* ---- basename / dirname (XPG, may modify the argument) ------------------- */

char *basename(char *path) {
    char *end, *slash;
    if (!path || !*path)
        return (char *)".";
    end = path + strlen(path);
    while (end > path && end[-1] == '/')   /* drop trailing slashes */
        --end;
    if (end == path)                        /* the path was all slashes */
        return (char *)"/";
    *end = '\0';
    slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

char *dirname(char *path) {
    size_t i;
    if (!path || !*path)
        return (char *)".";
    i = strlen(path) - 1;
    for (; path[i] == '/'; i--)             /* drop trailing slashes */
        if (!i) return (char *)"/";         /* the path was all slashes */
    for (; path[i] != '/'; i--)             /* drop the last component */
        if (!i) return (char *)".";         /* no directory part */
    for (; path[i] == '/'; i--)             /* drop the separator(s) */
        if (!i) return (char *)"/";         /* the parent is the root */
    path[i + 1] = '\0';
    return path;
}

/* ---- ffs ----------------------------------------------------------------- */

/* picolibc's libc/string already provides ffsl()/ffsll(); only plain ffs() is
   missing, so just that one is supplied here. */
int ffs(int i) {
    unsigned u = (unsigned)i;
    int n;
    if (u == 0)
        return 0;
    for (n = 1; !(u & 1u); ++n)
        u >>= 1;
    return n;
}
