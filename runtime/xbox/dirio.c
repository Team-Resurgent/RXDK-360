/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * Directory enumeration + extended path ops for picolibc on the Xbox 360 kernel,
 * ported from the original Xbox's libs/libc/xbox/dirio.c. This is the POSIX
 * surface libc++'s <filesystem> needs beyond fileio.c: <dirent.h>
 * (opendir/readdir/closedir) over NtQueryDirectoryFile, plus rename/truncate/
 * getcwd/chdir/realpath/lstat/statvfs/openat/unlinkat. Symlinks, hard links and
 * permissions do not exist on FATX, so those report ENOSYS or succeed as no-ops
 * honestly. The fd-based ops reuse fileio.c's descriptor table via
 * __rxdk_fd_handle/__rxdk_fd_install. Kernel-only (Nt/Rtl), never libxapi.
 */

#define _GNU_SOURCE 1  /* openat family, AT_* flags, statvfs, readlink under strict -std */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <unistd.h>

/* ---- NT kernel ABI (matches fileio.c) ------------------------------------- */

typedef long           NTSTATUS;
typedef void          *HANDLE;
typedef unsigned long  ULONG;
typedef unsigned short USHORT;

typedef struct { USHORT Length, MaximumLength; char *Buffer; } ANSI_STRING;
typedef struct { union { NTSTATUS Status; void *Pointer; }; unsigned long Information; } IO_STATUS_BLOCK;
typedef struct { HANDLE RootDirectory; ANSI_STRING *ObjectName; ULONG Attributes; } OBJECT_ATTRIBUTES;
typedef union { long long QuadPart; } LARGE_INTEGER;
typedef struct {
    ULONG NextEntryOffset, FileIndex;
    LARGE_INTEGER CreationTime, LastAccessTime, LastWriteTime, ChangeTime;
    LARGE_INTEGER EndOfFile, AllocationSize;
    ULONG FileAttributes, FileNameLength;
    char FileName[1];
} FILE_DIRECTORY_INFORMATION;
typedef struct { unsigned char ReplaceIfExists; HANDLE RootDirectory; ANSI_STRING FileName; } FILE_RENAME_INFORMATION;
typedef struct { LARGE_INTEGER EndOfFile; } FILE_END_OF_FILE_INFORMATION;

extern void     RtlInitAnsiString(ANSI_STRING *dst, const char *src);
extern NTSTATUS NtCreateFile(HANDLE *handle, ULONG access, OBJECT_ATTRIBUTES *obja,
                             IO_STATUS_BLOCK *iosb, LARGE_INTEGER *alloc,
                             ULONG attrs, ULONG share, ULONG disp, ULONG options);
extern NTSTATUS NtClose(HANDLE h);
extern NTSTATUS NtSetInformationFile(HANDLE h, IO_STATUS_BLOCK *iosb, void *info,
                                     ULONG len, int cls);
/* NB: the Xbox 360 NtQueryDirectoryFile has NO FileInformationClass parameter
   (unlike desktop NT and the original Xbox); it always returns
   FILE_DIRECTORY_INFORMATION. The arg order is handle, event, apc, apcctx,
   iosb, info, length, FileName, RestartScan. */
extern NTSTATUS NtQueryDirectoryFile(HANDLE h, HANDLE ev, void *apc, void *apcctx,
                                     IO_STATUS_BLOCK *iosb, void *info, ULONG len,
                                     ANSI_STRING *name, unsigned char restart);

/* shared fd table (fileio.c) + the plain file ops we build on */
extern void *__rxdk_fd_handle(int fd);
extern int   __rxdk_fd_install(void *h);
extern int   open(const char *path, int flags, ...);
extern int   close(int fd);
extern int   unlink(const char *path);
extern int   rmdir(const char *path);
extern int   stat(const char *path, struct stat *st);

#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#define NT_GENERIC_READ  0x80000000UL
#define NT_GENERIC_WRITE 0x40000000UL
#define NT_SYNCHRONIZE   0x00100000UL
#define NT_DELETE        0x00010000UL
#define NT_LIST_DIRECTORY 0x00000001UL
#define NT_SHARE_RWD     0x00000007UL
#define NT_FILE_OPEN     1UL
#define NT_DIRECTORY_FILE 0x00000001UL
#define NT_SYNCHRONOUS_IO 0x00000020UL
#define NT_ATTR_NORMAL   0x00000080UL
#define OBJ_CASE_INSENSITIVE 0x40UL
#define FILE_DIRECTORY_INFO   1
#define FILE_RENAME_INFO      10
#define FILE_END_OF_FILE_INFO 20
#define DIR_ATTR_DIRECTORY 0x10UL

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

/* '/'->'\'\; fully-qualified paths pass through (360 has no cwd default). */
static const char *fix_seps(const char *in, char *out, size_t n) {
    size_t i = 0;
    if (!in) return in;
    for (; in[i] && i + 1 < n; ++i) out[i] = (in[i] == '/') ? '\\' : in[i];
    out[i] = '\0';
    return out;
}

static NTSTATUS nt_open(const char *path, ULONG access, ULONG disp,
                        ULONG options, HANDLE *out) {
    ANSI_STRING name;
    OBJECT_ATTRIBUTES obja;
    IO_STATUS_BLOCK iosb;
    char buf[PATH_MAX];
    RtlInitAnsiString(&name, fix_seps(path, buf, sizeof buf));
    obja.RootDirectory = NULL;
    obja.ObjectName = &name;
    obja.Attributes = OBJ_CASE_INSENSITIVE;
    return NtCreateFile(out, access | NT_SYNCHRONIZE, &obja, &iosb, NULL,
                        NT_ATTR_NORMAL, NT_SHARE_RWD, disp,
                        options | NT_SYNCHRONOUS_IO);
}

/* ---- directory enumeration ------------------------------------------------ */

DIR *opendir(const char *path) {
    HANDLE h;
    int fd;
    DIR *d;
    if (!NT_SUCCESS(nt_open(path, NT_GENERIC_READ | NT_LIST_DIRECTORY,
                            NT_FILE_OPEN, NT_DIRECTORY_FILE, &h))) {
        errno = ENOENT; return NULL;
    }
    fd = __rxdk_fd_install(h);
    if (fd < 0) { NtClose(h); errno = EMFILE; return NULL; }
    d = (DIR *)malloc(sizeof(DIR));
    if (!d) { close(fd); errno = ENOMEM; return NULL; }
    d->fd = fd; d->offset = 0; d->count = 0;
    return d;
}

DIR *fdopendir(int fd) {
    DIR *d;
    if (!__rxdk_fd_handle(fd)) { errno = EBADF; return NULL; }
    d = (DIR *)malloc(sizeof(DIR));
    if (!d) { errno = ENOMEM; return NULL; }
    d->fd = fd; d->offset = 0; d->count = 0;
    return d;
}

struct dirent *readdir(DIR *d) {
    HANDLE h;
    IO_STATUS_BLOCK iosb;
    FILE_DIRECTORY_INFORMATION *fdi;
    ULONG nlen;
    size_t cap = sizeof(d->dirent.d_name) - 1;

    if (!d || !(h = __rxdk_fd_handle(d->fd))) { errno = EBADF; return NULL; }

    if (d->offset >= d->count) {   /* batch exhausted -> fetch next */
        /* The search spec is supplied as FindFirstFile does: "*" with
           RestartScan on the very first query (d->count still 0), then an
           EMPTY (never NULL) spec with RestartScan clear to continue where the
           enumeration left off. A NULL spec pointer is legal on NT but faults
           xenia (it dereferences the guest-null X_ANSI_STRING), and a non-empty
           spec makes xenia restart every call -- so the empty-spec continuation
           is what both agree on. */
        int is_first = (d->count == 0);
        ANSI_STRING spec;
        RtlInitAnsiString(&spec, is_first ? "*" : "");
        if (!NT_SUCCESS(NtQueryDirectoryFile(h, NULL, NULL, NULL, &iosb, d->buf,
                                             sizeof d->buf,
                                             &spec, (unsigned char)is_first))) {
            if (is_first) d->count = 0;   /* stay "not started" for rewinddir */
            return NULL;                  /* STATUS_NO_MORE_FILES / error -> end */
        }
        d->count = (size_t)iosb.Information;
        d->offset = 0;
        if (d->count == 0) return NULL;
    }

    fdi = (FILE_DIRECTORY_INFORMATION *)(d->buf + d->offset);
    nlen = fdi->FileNameLength;    /* bytes; FATX names are ANSI */
    if (nlen > cap) nlen = (ULONG)cap;
    memcpy(d->dirent.d_name, fdi->FileName, nlen);
    d->dirent.d_name[nlen] = '\0';
    d->dirent.d_ino = 0;
    d->dirent.d_type = (fdi->FileAttributes & DIR_ATTR_DIRECTORY) ? DT_DIR : DT_REG;

    if (fdi->NextEntryOffset) d->offset += fdi->NextEntryOffset;
    else                      d->offset = d->count;
    return &d->dirent;
}

int closedir(DIR *d) {
    if (!d) { errno = EBADF; return -1; }
    close(d->fd);
    free(d);
    return 0;
}

int dirfd(DIR *d) { if (!d) { errno = EINVAL; return -1; } return d->fd; }
void rewinddir(DIR *d) { if (d) { d->offset = 0; d->count = 0; } }

/* ---- rename / truncate ---------------------------------------------------- */

int rename(const char *oldp, const char *newp) {
    HANDLE h;
    IO_STATUS_BLOCK iosb;
    unsigned char info[sizeof(FILE_RENAME_INFORMATION) + PATH_MAX];
    FILE_RENAME_INFORMATION *ri = (FILE_RENAME_INFORMATION *)info;
    char nb[PATH_MAX];

    if (!NT_SUCCESS(nt_open(oldp, NT_DELETE | NT_SYNCHRONIZE, NT_FILE_OPEN, 0, &h))) {
        errno = ENOENT; return -1;
    }
    ri->ReplaceIfExists = 1;
    ri->RootDirectory = NULL;
    RtlInitAnsiString(&ri->FileName, fix_seps(newp, nb, sizeof nb));
    if (!NT_SUCCESS(NtSetInformationFile(h, &iosb, ri, sizeof info, FILE_RENAME_INFO))) {
        NtClose(h); errno = EIO; return -1;
    }
    NtClose(h);
    return 0;
}

int ftruncate(int fd, off_t length) {
    HANDLE h = __rxdk_fd_handle(fd);
    IO_STATUS_BLOCK iosb;
    FILE_END_OF_FILE_INFORMATION eof;
    if (!h) { errno = EBADF; return -1; }
    eof.EndOfFile.QuadPart = length;
    if (!NT_SUCCESS(NtSetInformationFile(h, &iosb, &eof, sizeof eof, FILE_END_OF_FILE_INFO))) {
        errno = EIO; return -1;
    }
    return 0;
}

int truncate(const char *path, off_t length) {
    HANDLE h;
    IO_STATUS_BLOCK iosb;
    FILE_END_OF_FILE_INFORMATION eof;
    if (!NT_SUCCESS(nt_open(path, NT_GENERIC_WRITE, NT_FILE_OPEN, 0, &h))) {
        errno = ENOENT; return -1;
    }
    eof.EndOfFile.QuadPart = length;
    if (!NT_SUCCESS(NtSetInformationFile(h, &iosb, &eof, sizeof eof, FILE_END_OF_FILE_INFO))) {
        NtClose(h); errno = EIO; return -1;
    }
    NtClose(h);
    return 0;
}

/* ---- stat variants / realpath / cwd --------------------------------------- */

int lstat(const char *path, struct stat *st) { return stat(path, st); }  /* no symlinks */

char *realpath(const char *path, char *resolved) {
    char tmp[PATH_MAX];
    const char *r = fix_seps(path, tmp, sizeof tmp);
    size_t n = strlen(r);
    if (!resolved) { resolved = (char *)malloc(n + 1); if (!resolved) return NULL; }
    memcpy(resolved, r, n + 1);
    return resolved;
}

/* The 360 has no per-process cwd; libc tracks one so relative-path portable code
   has a base. Defaults to the read-only title media. */
static char g_cwd[PATH_MAX] = "game:\\";

char *getcwd(char *buf, size_t size) {
    size_t n = strlen(g_cwd);
    if (!buf) { buf = (char *)malloc(n + 1); if (!buf) return NULL; }
    else if (size <= n) { errno = ERANGE; return NULL; }
    memcpy(buf, g_cwd, n + 1);
    return buf;
}

int chdir(const char *path) {
    char tmp[PATH_MAX];
    const char *r = fix_seps(path, tmp, sizeof tmp);
    size_t n = strlen(r);
    if (n + 1 >= sizeof g_cwd) { errno = ENAMETOOLONG; return -1; }
    memcpy(g_cwd, r, n + 1);
    return 0;
}

/* ---- statvfs (volume free/total, best-effort zero) ------------------------ */

int statvfs(const char *path, struct statvfs *b) {
    (void)path;
    if (!b) { errno = EINVAL; return -1; }
    memset(b, 0, sizeof *b);
    b->f_bsize = b->f_frsize = 4096;   /* FATX cluster-ish; sizes unknown here */
    return 0;
}

/* ---- *at() ops (only AT_FDCWD; the 360 has no dirfd-relative open) --------- */

int openat(int dirfd, const char *path, int flags, ...) {
    if (dirfd == AT_FDCWD) return open(path, flags);
    errno = ENOSYS; return -1;
}
int unlinkat(int dirfd, const char *path, int flag) {
    if (dirfd != AT_FDCWD) { errno = ENOSYS; return -1; }
    return (flag & AT_REMOVEDIR) ? rmdir(path) : unlink(path);
}

/* ---- unsupported on FATX: honest failures / no-ops ------------------------ */

int fchmod(int fd, mode_t mode) { (void)fd; (void)mode; return 0; }   /* no perms */
int fchmodat(int dirfd, const char *path, mode_t mode, int flag) {
    (void)dirfd; (void)path; (void)mode; (void)flag; return 0;
}
int utimes(const char *path, const struct timeval times[2]) {
    (void)path; (void)times; return 0;                                /* accept, ignore */
}
long pathconf(const char *path, int name) { (void)path; (void)name; errno = EINVAL; return -1; }
int symlink(const char *target, const char *linkpath) {
    (void)target; (void)linkpath; errno = ENOSYS; return -1;
}
ssize_t readlink(const char *path, char *buf, size_t bufsize) {
    (void)path; (void)buf; (void)bufsize; errno = EINVAL; return -1;  /* not a symlink */
}
int link(const char *oldpath, const char *newpath) {
    (void)oldpath; (void)newpath; errno = ENOSYS; return -1;
}
