/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * ftw()/nftw() -- walk a file tree, invoking a callback per entry. Built on the
 * runtime's opendir/readdir + stat. ftw is pre-order; nftw additionally honours
 * FTW_DEPTH (post-order directory visits, type FTW_DP) and passes a struct FTW.
 * There are no symlinks on FATX/GDFX, so FTW_SL/FTW_SLN are never produced and
 * FTW_PHYS is a no-op. nopenfd is accepted and ignored (we open one dir at a
 * time). '/' and '\' both count as separators.
 */
#include <ftw.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

extern int stat(const char *path, struct stat *st);

typedef int (*ftw_cb)(const char *, const struct stat *, int);
typedef int (*nftw_cb)(const char *, const struct stat *, int, struct FTW *);

static int is_sep(char c) { return c == '/' || c == '\\'; }

static int do_walk(char *path, int level, int flags, ftw_cb f1, nftw_cb f2) {
    struct stat st;
    struct FTW  fw;
    DIR        *d = NULL;
    int         type, rc;
    size_t      i, plen = strlen(path);

    for (i = plen; i > 0 && !is_sep(path[i - 1]); i--)
        ;
    fw.base = (int)i;
    fw.level = level;

    if (stat(path, &st) != 0) {
        type = FTW_NS;
    } else if (S_ISDIR(st.st_mode)) {
        d = opendir(path);
        type = d ? FTW_D : FTW_DNR;
    } else {
        type = FTW_F;
    }

    /* pre-order visit (deferred to post-order for a directory in FTW_DEPTH) */
    if (!(type == FTW_D && (flags & FTW_DEPTH))) {
        rc = f2 ? f2(path, &st, type, &fw) : f1(path, &st, type);
        if (rc != 0) { if (d) closedir(d); return rc; }
    }

    if (type == FTW_D) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            char   child[PATH_MAX];
            size_t nlen = strlen(e->d_name);
            size_t cp;
            if (e->d_name[0] == '.' &&
                (e->d_name[1] == '\0' || (e->d_name[1] == '.' && e->d_name[2] == '\0')))
                continue;                   /* skip "." and ".." */
            if (plen + 1 + nlen + 1 > sizeof child)
                continue;
            memcpy(child, path, plen);
            cp = plen;
            if (cp && !is_sep(child[cp - 1]))
                child[cp++] = '/';
            memcpy(child + cp, e->d_name, nlen + 1);
            rc = do_walk(child, level + 1, flags, f1, f2);
            if (rc != 0) { closedir(d); return rc; }
        }
        closedir(d);

        if ((flags & FTW_DEPTH) && f2) {    /* post-order directory visit */
            rc = f2(path, &st, FTW_DP, &fw);
            if (rc != 0) return rc;
        }
    }
    return 0;
}

int ftw(const char *path, ftw_cb fn, int nopenfd) {
    char buf[PATH_MAX];
    (void)nopenfd;
    if (!path || strlen(path) >= sizeof buf) { errno = ENAMETOOLONG; return -1; }
    strcpy(buf, path);
    return do_walk(buf, 0, 0, fn, (nftw_cb)0);
}

int nftw(const char *path, nftw_cb fn, int nopenfd, int flags) {
    char buf[PATH_MAX];
    (void)nopenfd;
    if (!path || strlen(path) >= sizeof buf) { errno = ENAMETOOLONG; return -1; }
    strcpy(buf, path);
    return do_walk(buf, 0, flags, (ftw_cb)0, fn);
}
