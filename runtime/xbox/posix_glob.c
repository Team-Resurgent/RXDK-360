/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * Filename pattern matching for portable POSIX code:
 *
 *   - fnmatch(): the shell-glob matcher (*, ?, [..], \ escaping) with the
 *     standard FNM_PATHNAME / FNM_PERIOD / FNM_NOESCAPE / FNM_CASEFOLD /
 *     FNM_LEADING_DIR flags. Self-contained (recursive descent, the classic
 *     BSD structure) -- picolibc leaves it to the platform.
 *   - glob(): expands one pattern against the file system via our own
 *     opendir/readdir + fnmatch. The last '/'-separated component is the glob;
 *     the leading part is taken as a literal directory (no multi-level wildcard
 *     or brace/tilde expansion -- neither is meaningful against the console's
 *     drive-qualified paths). Honours GLOB_MARK / GLOB_NOSORT / GLOB_NOCHECK /
 *     GLOB_APPEND / GLOB_DOOFFS / GLOB_ERR.
 *   - select()/pselect(): the console's file descriptors never block -- an
 *     NtReadFile/NtWriteFile completes synchronously -- so every requested fd is
 *     reported ready immediately and the timeout is irrelevant.
 */
#define _GNU_SOURCE 1
#include <fnmatch.h>
#include <glob.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* ---- fnmatch ------------------------------------------------------------- */

#define RANGE_MATCH   1
#define RANGE_NOMATCH 0
#define RANGE_ERROR   (-1)

static int fold(int c, int flags) {
    if ((flags & FNM_CASEFOLD) && c >= 'A' && c <= 'Z')
        return c - 'A' + 'a';
    return c;
}

static int rangematch(const char *pattern, int test, int flags, const char **newp) {
    int negate, ok;
    char c, c2;

    if ((negate = (*pattern == '!' || *pattern == '^')))
        ++pattern;
    test = fold((unsigned char)test, flags);
    ok = 0;
    while ((c = *pattern++) != ']') {
        if (c == '\\' && !(flags & FNM_NOESCAPE))
            c = *pattern++;
        if (c == '\0')
            return RANGE_ERROR;                 /* no closing bracket */
        c = fold((unsigned char)c, flags);
        if (*pattern == '-' && (c2 = *(pattern + 1)) != ']' && c2 != '\0') {
            pattern += 2;
            if (c2 == '\\' && !(flags & FNM_NOESCAPE))
                c2 = *pattern++;
            if (c2 == '\0')
                return RANGE_ERROR;
            c2 = fold((unsigned char)c2, flags);
            if ((unsigned char)c <= test && test <= (unsigned char)c2)
                ok = 1;
        } else if ((unsigned char)c == test) {
            ok = 1;
        }
    }
    *newp = pattern;
    return (ok == negate ? RANGE_NOMATCH : RANGE_MATCH);
}

/* leading '.' that FNM_PERIOD forbids a wildcard from matching */
static int leading_period(const char *s, const char *start, int flags) {
    if (!(flags & FNM_PERIOD) || *s != '.')
        return 0;
    return (s == start || ((flags & FNM_PATHNAME) && *(s - 1) == '/'));
}

int fnmatch(const char *pattern, const char *string, int flags) {
    const char *stringstart = string;
    const char *newp;
    char c, test;

    for (;;) {
        switch (c = *pattern++) {
        case '\0':
            if ((flags & FNM_LEADING_DIR) && *string == '/')
                return 0;
            return (*string == '\0') ? 0 : FNM_NOMATCH;

        case '?':
            if (*string == '\0')
                return FNM_NOMATCH;
            if (*string == '/' && (flags & FNM_PATHNAME))
                return FNM_NOMATCH;
            if (leading_period(string, stringstart, flags))
                return FNM_NOMATCH;
            ++string;
            break;

        case '*':
            c = *pattern;
            while (c == '*')
                c = *++pattern;                 /* collapse a run of '*' */
            if (leading_period(string, stringstart, flags))
                return FNM_NOMATCH;
            if (c == '\0') {                    /* trailing '*' */
                if (flags & FNM_PATHNAME)
                    return ((flags & FNM_LEADING_DIR) ||
                            strchr(string, '/') == NULL) ? 0 : FNM_NOMATCH;
                return 0;
            }
            if (c == '/' && (flags & FNM_PATHNAME)) {
                if ((string = strchr(string, '/')) == NULL)
                    return FNM_NOMATCH;
                break;
            }
            while ((test = *string) != '\0') {  /* general back-tracking case */
                if (!fnmatch(pattern, string, flags & ~FNM_PERIOD))
                    return 0;
                if (test == '/' && (flags & FNM_PATHNAME))
                    break;
                ++string;
            }
            return FNM_NOMATCH;

        case '[':
            if (*string == '\0')
                return FNM_NOMATCH;
            if (*string == '/' && (flags & FNM_PATHNAME))
                return FNM_NOMATCH;
            if (leading_period(string, stringstart, flags))
                return FNM_NOMATCH;
            switch (rangematch(pattern, (unsigned char)*string, flags, &newp)) {
            case RANGE_ERROR:
                goto literal;                   /* '[' with no ']' is a literal */
            case RANGE_MATCH:
                pattern = newp;
                break;
            case RANGE_NOMATCH:
                return FNM_NOMATCH;
            }
            ++string;
            break;

        case '\\':
            if (!(flags & FNM_NOESCAPE)) {
                if ((c = *pattern++) == '\0') {
                    c = '\\';
                    --pattern;
                }
            }
            /* FALLTHROUGH */
        default:
        literal:
            if (fold((unsigned char)c, flags) != fold((unsigned char)*string, flags))
                return FNM_NOMATCH;
            ++string;
            break;
        }
    }
}

/* ---- glob ---------------------------------------------------------------- */

static int str_cmp(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static char *dupstr(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

int glob(const char *pattern, int flags, int (*errfunc)(const char *, int),
         glob_t *pg) {
    const char *slash = strrchr(pattern, '/');
    const char *base  = slash ? slash + 1 : pattern;
    size_t      preflen = slash ? (size_t)(slash - pattern + 1) : 0; /* incl. '/' */
    char        opendirpath[520];
    DIR        *dir;
    struct dirent *ent;
    char      **found = NULL;
    size_t      nfound = 0, cap = 0;
    int         fnflags = FNM_PERIOD;            /* matching one basename, no '/' */
    size_t      i, base0;

    if (!(flags & GLOB_APPEND)) {
        pg->gl_pathc = 0;
        pg->gl_matchc = 0;
        pg->gl_offs = (flags & GLOB_DOOFFS) ? pg->gl_offs : 0;
        pg->gl_pathv = NULL;
        pg->gl_flags = flags;
    }

    /* directory to scan: literal prefix, or "." when the pattern has no '/' */
    if (!slash) {
        strcpy(opendirpath, ".");
    } else if (preflen == 1) {
        strcpy(opendirpath, "/");
    } else {
        if (preflen - 1 >= sizeof opendirpath)
            return GLOB_NOSPACE;
        memcpy(opendirpath, pattern, preflen - 1);
        opendirpath[preflen - 1] = '\0';
    }

    dir = opendir(opendirpath);
    if (!dir) {
        if ((flags & GLOB_ERR) || (errfunc && errfunc(opendirpath, errno)))
            return GLOB_ABEND;
        goto nomatch;                            /* unreadable dir -> no matches */
    }

    while ((ent = readdir(dir)) != NULL) {
        char full[540];
        size_t nlen = strlen(ent->d_name);

        if (fnmatch(base, ent->d_name, fnflags) != 0)
            continue;
        if (preflen + nlen + 2 >= sizeof full)
            continue;
        memcpy(full, pattern, preflen);          /* the literal directory part */
        memcpy(full + preflen, ent->d_name, nlen);
        full[preflen + nlen] = '\0';
        if ((flags & GLOB_MARK) && ent->d_type == DT_DIR) {
            full[preflen + nlen] = '/';
            full[preflen + nlen + 1] = '\0';
        }

        if (nfound == cap) {
            size_t ncap = cap ? cap * 2 : 16;
            char **np = (char **)realloc(found, ncap * sizeof(char *));
            if (!np) { closedir(dir); free(found); return GLOB_NOSPACE; }
            found = np;
            cap = ncap;
        }
        if (!(found[nfound] = dupstr(full))) {
            closedir(dir);
            free(found);
            return GLOB_NOSPACE;
        }
        ++nfound;
    }
    closedir(dir);

    if (nfound == 0) {
nomatch:
        if (flags & GLOB_NOCHECK) {              /* return the pattern itself */
            found = (char **)malloc(sizeof(char *));
            if (!found)
                return GLOB_NOSPACE;
            if (!(found[0] = dupstr(pattern))) {
                free(found);
                return GLOB_NOSPACE;
            }
            nfound = 1;
        } else {
            free(found);
            return GLOB_NOMATCH;
        }
    } else if (!(flags & GLOB_NOSORT)) {
        qsort(found, nfound, sizeof(char *), str_cmp);
    }

    /* splice into gl_pathv: [gl_offs NULLs][previous][new][NULL] */
    base0 = (size_t)pg->gl_offs + (size_t)pg->gl_pathc;
    {
        char **v = (char **)realloc(pg->gl_pathv,
                                    (base0 + nfound + 1) * sizeof(char *));
        if (!v) {
            for (i = 0; i < nfound; i++)
                free(found[i]);
            free(found);
            return GLOB_NOSPACE;
        }
        if (!pg->gl_pathv)                       /* first call: clear gl_offs slots */
            for (i = 0; i < (size_t)pg->gl_offs; i++)
                v[i] = NULL;
        pg->gl_pathv = v;
    }
    for (i = 0; i < nfound; i++)
        pg->gl_pathv[base0 + i] = found[i];
    pg->gl_pathv[base0 + nfound] = NULL;
    pg->gl_pathc += (int)nfound;
    pg->gl_matchc += (int)nfound;
    free(found);
    return 0;
}

void globfree(glob_t *pg) {
    if (!pg || !pg->gl_pathv)
        return;
    for (int i = 0; i < pg->gl_pathc; i++)
        free(pg->gl_pathv[pg->gl_offs + i]);
    free(pg->gl_pathv);
    pg->gl_pathv = NULL;
    pg->gl_pathc = 0;
    pg->gl_matchc = 0;
}

/* ---- select / pselect ---------------------------------------------------- */

/* Console file descriptors complete every read/write synchronously, so they are
   never "not ready". Report each requested read/write fd ready at once; there
   are no exceptional conditions. The timeout never matters -- we return now. */
static int select_ready(int n, fd_set *rd, fd_set *wr, fd_set *ex) {
    int count = 0, fd;
    if (n > FD_SETSIZE)
        n = FD_SETSIZE;
    for (fd = 0; fd < n; fd++) {
        if (rd && FD_ISSET(fd, rd))
            ++count;                             /* left set: still ready */
        if (wr && FD_ISSET(fd, wr))
            ++count;
    }
    if (ex)
        FD_ZERO(ex);
    return count;
}

int select(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout) {
    (void)timeout;
    return select_ready(n, readfds, writefds, exceptfds);
}

int pselect(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
            const struct timespec *timeout, const sigset_t *sigmask) {
    (void)timeout; (void)sigmask;
    return select_ready(n, readfds, writefds, exceptfds);
}
