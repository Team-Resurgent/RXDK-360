/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * POSIX file I/O backend for picolibc on the Xbox 360 kernel, ported from the
 * original Xbox's libs/libc/xbox/fileio.c. This is the storage layer picolibc
 * stdio calls into (fopen/fread/fwrite/fseek/...) and what libc++'s <fstream>
 * sits on. It uses ONLY kernel exports (Nt/Rtl), never libxapi, so the
 * libc -> kernel layering stays acyclic.
 *
 * Paths are fully-qualified drive paths (game:\..., cache:\..., ...) with
 * backslash separators -- the 360 file system has no current-directory or
 * current-drive default (XDK "File Name Conventions"), so we do not synthesise
 * one; we only translate '/' to '\' for callers (libc++ <filesystem>) that join
 * with forward slashes. game:\ is the read-only title media; cache:\ is the
 * writable per-title utility drive (mounted via XMountUtilityDrive).
 *
 * fds 0/1/2 are the console: read(0)/write(1,2) route through the registered
 * libc hooks (libc_hooks.c), falling back to DbgPrint / EOF -- so this file
 * supersedes the read/write/lseek/close stubs libc_hooks.c used to provide.
 */

#define _GNU_SOURCE 1  /* picolibc gates open/O_*/struct stat behind POSIX visibility */

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "libc_hooks.h"

extern int DbgPrint(const char *fmt, ...);

/* ---- NT kernel ABI (self-contained; matches the 360 xboxkrnl layout) ------ */

typedef long           NTSTATUS;
typedef void          *HANDLE;
typedef unsigned long  ULONG;
typedef unsigned short USHORT;

typedef struct { USHORT Length, MaximumLength; char *Buffer; } ANSI_STRING;
typedef struct { union { NTSTATUS Status; void *Pointer; }; unsigned long Information; } IO_STATUS_BLOCK;
/* The 360 OBJECT_ATTRIBUTES: RootDirectory, ObjectName, Attributes (no Length /
   SecurityDescriptor, unlike desktop NT). */
typedef struct { HANDLE RootDirectory; ANSI_STRING *ObjectName; ULONG Attributes; } OBJECT_ATTRIBUTES;
typedef union { long long QuadPart; } LARGE_INTEGER;
typedef struct {
    LARGE_INTEGER CreationTime, LastAccessTime, LastWriteTime, ChangeTime;
    LARGE_INTEGER AllocationSize, EndOfFile;
    ULONG FileAttributes;
} FILE_NETWORK_OPEN_INFORMATION;
typedef struct { unsigned char DeleteFile; } FILE_DISPOSITION_INFORMATION;

extern void     RtlInitAnsiString(ANSI_STRING *dst, const char *src);
extern NTSTATUS NtCreateFile(HANDLE *handle, ULONG access, OBJECT_ATTRIBUTES *obja,
                             IO_STATUS_BLOCK *iosb, LARGE_INTEGER *alloc,
                             ULONG attrs, ULONG share, ULONG disp, ULONG options);
extern NTSTATUS NtReadFile(HANDLE h, HANDLE ev, void *apc, void *apcctx,
                           IO_STATUS_BLOCK *iosb, void *buf, ULONG len, LARGE_INTEGER *off);
extern NTSTATUS NtWriteFile(HANDLE h, HANDLE ev, void *apc, void *apcctx,
                            IO_STATUS_BLOCK *iosb, void *buf, ULONG len, LARGE_INTEGER *off);
extern NTSTATUS NtClose(HANDLE h);
extern NTSTATUS NtQueryInformationFile(HANDLE h, IO_STATUS_BLOCK *iosb, void *info,
                                       ULONG len, int cls);
extern NTSTATUS NtQueryFullAttributesFile(OBJECT_ATTRIBUTES *obja,
                                          FILE_NETWORK_OPEN_INFORMATION *info);
extern NTSTATUS NtSetInformationFile(HANDLE h, IO_STATUS_BLOCK *iosb, void *info,
                                     ULONG len, int cls);

#define NT_SUCCESS(s)  ((NTSTATUS)(s) >= 0)
#define STATUS_END_OF_FILE ((NTSTATUS)0xC0000011L)

#define NT_GENERIC_READ   0x80000000UL
#define NT_GENERIC_WRITE  0x40000000UL
#define NT_SYNCHRONIZE    0x00100000UL
#define NT_READ_ATTRS     0x00000080UL
#define NT_DELETE         0x00010000UL
#define NT_SHARE_RWD      0x00000007UL   /* READ|WRITE|DELETE */
#define NT_FILE_OPEN         1UL
#define NT_FILE_CREATE       2UL
#define NT_FILE_OPEN_IF      3UL
#define NT_FILE_OVERWRITE    4UL
#define NT_FILE_OVERWRITE_IF 5UL
#define NT_DIRECTORY_FILE     0x00000001UL
#define NT_SYNCHRONOUS_IO     0x00000020UL
#define NT_NON_DIRECTORY_FILE 0x00000040UL
#define NT_ATTR_NORMAL        0x00000080UL
#define NT_ATTR_DIRECTORY     0x00000010UL
#define OBJ_CASE_INSENSITIVE  0x00000040UL
#define FILE_NETWORK_OPEN_INFO 34
#define FILE_DISPOSITION_INFO  13

/* ---- fd table -------------------------------------------------------------- */

#define RXDK_FD_BASE 3
#define RXDK_FD_MAX  32

typedef struct {
    HANDLE    handle;
    long long offset;
    int       append;
} rxdk_ofd;

static rxdk_ofd *fd_table[RXDK_FD_MAX];   /* NULL = free */

static int alloc_slot(rxdk_ofd *o) {
    for (int i = RXDK_FD_BASE; i < RXDK_FD_MAX; ++i)
        if (!fd_table[i]) { fd_table[i] = o; return i; }
    return -1;
}
static rxdk_ofd *get_fd(int fd) {
    return (fd < RXDK_FD_BASE || fd >= RXDK_FD_MAX) ? NULL : fd_table[fd];
}

/* Shared with dirio.c (directory ops live there but reuse this fd table): map an
   fd to its kernel handle, and install a bare handle as a new file fd. */
void *__rxdk_fd_handle(int fd) {
    rxdk_ofd *o = get_fd(fd);
    return o ? o->handle : (void *)0;
}
int __rxdk_fd_install(void *h) {
    rxdk_ofd *o = (rxdk_ofd *)malloc(sizeof *o);
    int fd;
    if (!o) return -1;
    o->handle = (HANDLE)h;
    o->offset = 0;
    o->append = 0;
    fd = alloc_slot(o);
    if (fd < 0) { free(o); return -1; }
    return fd;
}

/* Translate '/' to '\' into a caller buffer; fully-qualified paths pass through
   unchanged (the 360 has no cwd/default drive). */
#define RXDK_PATH_BUF 1024
static const char *fix_seps(const char *in, char *out, size_t n) {
    size_t i = 0;
    if (!in) return in;
    for (; in[i] && i + 1 < n; ++i)
        out[i] = (in[i] == '/') ? '\\' : in[i];
    out[i] = '\0';
    return out;
}

static NTSTATUS nt_open(const char *path, ULONG access, ULONG disp,
                        ULONG options, HANDLE *out) {
    ANSI_STRING name;
    OBJECT_ATTRIBUTES obja;
    IO_STATUS_BLOCK iosb;
    char buf[RXDK_PATH_BUF];

    RtlInitAnsiString(&name, fix_seps(path, buf, sizeof buf));
    obja.RootDirectory = NULL;   /* the drive path is fully-qualified */
    obja.ObjectName = &name;
    obja.Attributes = OBJ_CASE_INSENSITIVE;
    return NtCreateFile(out, access | NT_SYNCHRONIZE | NT_READ_ATTRS, &obja,
                        &iosb, NULL, NT_ATTR_NORMAL, NT_SHARE_RWD, disp,
                        options | NT_SYNCHRONOUS_IO);
}

static long long file_size(HANDLE h) {
    /* The Xbox kernel rejects FileStandardInformation; FileNetworkOpenInformation
       carries EndOfFile and is supported. */
    FILE_NETWORK_OPEN_INFORMATION info;
    IO_STATUS_BLOCK iosb;
    if (!NT_SUCCESS(NtQueryInformationFile(h, &iosb, &info, sizeof info,
                                           FILE_NETWORK_OPEN_INFO)))
        return -1;
    return info.EndOfFile.QuadPart;
}

/* ---- open / read / write / lseek / close ---------------------------------- */

int open(const char *path, int flags, ...) {
    ULONG access, disp;
    HANDLE h;
    rxdk_ofd *o;
    int fd;

    switch (flags & O_ACCMODE) {
    case O_RDONLY: access = NT_GENERIC_READ; break;
    case O_WRONLY: access = NT_GENERIC_WRITE; break;
    default:       access = NT_GENERIC_READ | NT_GENERIC_WRITE; break;
    }
    if (flags & O_CREAT) {
        if (flags & O_EXCL)       disp = NT_FILE_CREATE;
        else if (flags & O_TRUNC) disp = NT_FILE_OVERWRITE_IF;
        else                      disp = NT_FILE_OPEN_IF;
    } else {
        disp = (flags & O_TRUNC) ? NT_FILE_OVERWRITE : NT_FILE_OPEN;
    }

    if (!NT_SUCCESS(nt_open(path, access, disp, NT_NON_DIRECTORY_FILE, &h))) {
        errno = ENOENT;
        return -1;
    }
    o = (rxdk_ofd *)malloc(sizeof *o);
    if (!o) { NtClose(h); errno = ENOMEM; return -1; }
    o->handle = h;
    o->offset = 0;
    o->append = (flags & O_APPEND) ? 1 : 0;
    if (o->append) { long long sz = file_size(h); o->offset = sz > 0 ? sz : 0; }

    fd = alloc_slot(o);
    if (fd < 0) { NtClose(h); free(o); errno = EMFILE; return -1; }
    return fd;
}

ssize_t read(int fd, void *buf, size_t count) {
    rxdk_ofd *o;
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER off;
    NTSTATUS st;

    if (fd == 0)
        return __rxdk_stdin_hook ? __rxdk_stdin_hook(buf, count) : 0;  /* EOF */

    o = get_fd(fd);
    if (!o) { errno = EBADF; return -1; }
    if (count == 0) return 0;

    off.QuadPart = o->offset;
    st = NtReadFile(o->handle, NULL, NULL, NULL, &iosb, buf, (ULONG)count, &off);
    if (st == STATUS_END_OF_FILE) return 0;
    if (!NT_SUCCESS(st)) { errno = EIO; return -1; }
    o->offset += (long long)iosb.Information;
    return (ssize_t)iosb.Information;
}

ssize_t write(int fd, const void *buf, size_t count) {
    rxdk_ofd *o;
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER off;
    NTSTATUS st;

    if (!buf || count == 0) return 0;

    if (fd >= 0 && fd < RXDK_FD_BASE) {          /* console: hook, else DbgPrint */
        char line[512];
        size_t n = count;
        if (__rxdk_output_hook)
            return __rxdk_output_hook(fd, buf, count);
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, buf, n);
        line[n] = '\0';
        DbgPrint("%s", line);
        return (ssize_t)count;
    }

    o = get_fd(fd);
    if (!o) { errno = EBADF; return -1; }
    if (o->append) { long long sz = file_size(o->handle); if (sz >= 0) o->offset = sz; }
    off.QuadPart = o->offset;
    st = NtWriteFile(o->handle, NULL, NULL, NULL, &iosb, (void *)(size_t)buf,
                     (ULONG)count, &off);
    if (!NT_SUCCESS(st)) { errno = EIO; return -1; }
    o->offset += (long long)iosb.Information;
    return (ssize_t)iosb.Information;
}

off_t lseek(int fd, off_t offset, int whence) {
    rxdk_ofd *o = get_fd(fd);
    long long base, pos;
    if (!o) { errno = EBADF; return -1; }
    switch (whence) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = o->offset; break;
    case SEEK_END: base = file_size(o->handle); if (base < 0) base = 0; break;
    default: errno = EINVAL; return -1;
    }
    pos = base + (long long)offset;
    if (pos < 0) { errno = EINVAL; return -1; }
    o->offset = pos;
    return (off_t)pos;
}

int close(int fd) {
    rxdk_ofd *o;
    if (fd >= 0 && fd < RXDK_FD_BASE) return 0;   /* console */
    o = get_fd(fd);
    if (!o) { errno = EBADF; return -1; }
    fd_table[fd] = NULL;
    if (o->handle) NtClose(o->handle);
    free(o);
    return 0;
}

/* ---- stat / unlink / mkdir / rmdir ---------------------------------------- */

static void set_stat(struct stat *st, long long size, unsigned long attrs) {
    memset(st, 0, sizeof *st);
    st->st_size = (off_t)size;
    st->st_mode = (attrs & NT_ATTR_DIRECTORY) ? S_IFDIR : S_IFREG;
}

int fstat(int fd, struct stat *st) {
    rxdk_ofd *o = get_fd(fd);
    FILE_NETWORK_OPEN_INFORMATION info;
    IO_STATUS_BLOCK iosb;
    long long size = 0;
    unsigned long attrs = 0;
    if (!o || !st) { errno = EBADF; return -1; }
    if (NT_SUCCESS(NtQueryInformationFile(o->handle, &iosb, &info, sizeof info,
                                          FILE_NETWORK_OPEN_INFO))) {
        size = info.EndOfFile.QuadPart;
        attrs = info.FileAttributes;
    }
    set_stat(st, size, attrs);
    return 0;
}

int stat(const char *path, struct stat *st) {
    HANDLE h;
    IO_STATUS_BLOCK iosb;
    FILE_NETWORK_OPEN_INFORMATION info;
    long long size = 0;
    unsigned long attrs = 0;
    if (!st) { errno = EINVAL; return -1; }
    /* Open (access=0 -> just SYNCHRONIZE|READ_ATTRIBUTES from nt_open, which
       opens files AND directories) and query the HANDLE. The path-based
       NtQueryFullAttributesFile reports EndOfFile from the cached directory
       entry, which is stale under xenia for a file just written through a
       handle; the handle query returns the live size. */
    if (!NT_SUCCESS(nt_open(path, 0, NT_FILE_OPEN, 0, &h))) { errno = ENOENT; return -1; }
    if (NT_SUCCESS(NtQueryInformationFile(h, &iosb, &info, sizeof info,
                                          FILE_NETWORK_OPEN_INFO))) {
        size = info.EndOfFile.QuadPart;
        attrs = info.FileAttributes;
    }
    NtClose(h);
    set_stat(st, size, attrs);
    return 0;
}

static int nt_delete(const char *path, ULONG options) {
    HANDLE h;
    IO_STATUS_BLOCK iosb;
    FILE_DISPOSITION_INFORMATION dispose;
    if (!NT_SUCCESS(nt_open(path, NT_DELETE, NT_FILE_OPEN, options, &h))) {
        errno = ENOENT; return -1;
    }
    dispose.DeleteFile = 1;
    if (!NT_SUCCESS(NtSetInformationFile(h, &iosb, &dispose, sizeof dispose,
                                         FILE_DISPOSITION_INFO))) {
        NtClose(h); errno = EIO; return -1;
    }
    NtClose(h);
    return 0;
}

int unlink(const char *path) { return nt_delete(path, NT_NON_DIRECTORY_FILE); }
int rmdir(const char *path)  { return nt_delete(path, NT_DIRECTORY_FILE); }

int mkdir(const char *path, mode_t mode) {
    HANDLE h;
    (void)mode;
    if (!NT_SUCCESS(nt_open(path, NT_GENERIC_READ, NT_FILE_CREATE,
                            NT_DIRECTORY_FILE, &h))) {
        errno = EEXIST; return -1;
    }
    NtClose(h);
    return 0;
}
