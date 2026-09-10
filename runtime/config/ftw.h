/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 *
 * <ftw.h> -- picolibc ships no such header. ftw()/nftw() walk a directory tree
 * over the runtime's opendir/readdir + stat. Implemented in runtime/xbox/ftw.c.
 */
#ifndef _RXDK_FTW_H_
#define _RXDK_FTW_H_

#include <sys/types.h>
#include <sys/stat.h>

/* type passed to the callback */
#define FTW_F   0   /* regular file */
#define FTW_D   1   /* directory (pre-order) */
#define FTW_DNR 2   /* directory that could not be read */
#define FTW_NS  3   /* stat() failed */
#define FTW_SL  4   /* symbolic link (never produced: no symlinks) */
#define FTW_DP  5   /* directory, post-order (nftw + FTW_DEPTH) */
#define FTW_SLN 6   /* dangling symlink (never produced) */

/* nftw() flags */
#define FTW_PHYS  1
#define FTW_MOUNT 2
#define FTW_CHDIR 4
#define FTW_DEPTH 8

struct FTW {
    int base;   /* offset of the last path component */
    int level;  /* depth below the starting path (0 = the path itself) */
};

#ifdef __cplusplus
extern "C" {
#endif

int ftw(const char *path, int (*fn)(const char *, const struct stat *, int),
        int nopenfd);
int nftw(const char *path,
         int (*fn)(const char *, const struct stat *, int, struct FTW *),
         int nopenfd, int flags);

#ifdef __cplusplus
}
#endif

#endif /* _RXDK_FTW_H_ */
