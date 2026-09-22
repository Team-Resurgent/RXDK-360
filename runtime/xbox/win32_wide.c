/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * RXDK-360 Win32 wide (*W) forwarders.
 *
 * The XDK implements its Win32 file/synchronization/module APIs as ANSI (*A) only
 * (see runtime/config/rxdk_win32_wide.h for the why). This unit provides the
 * link-side twin of that header: the complete *W family, each narrowing its wide
 * arguments to the *A entry the console actually exports and widening any wide
 * out-parameter back. Xbox file-system paths and object names are ASCII, so a
 * byte-wise narrow/widen is exact for the inputs these APIs see.
 *
 * Self-contained like the other bridges (ms_printf.c, xdk_cpp_bridge.cpp): the
 * runtime is built without the XDK headers on its include path, so the Win32 ABI
 * types are reproduced here to match winnt.h/winbase.h exactly (only layout and
 * calling convention matter -- there is one calling convention on this PPC
 * target, so WINAPI carries no attribute).
 */

typedef char                CHAR;
typedef unsigned short      WCHAR;      /* 16-bit, matches -fshort-wchar / XDK WCHAR */
typedef int                 BOOL;
typedef unsigned long       DWORD;
typedef long                LONG;
typedef void               *HANDLE;
typedef void               *HMODULE;
typedef void               *LPVOID;
typedef const char         *LPCSTR;
typedef char               *LPSTR;
typedef const WCHAR        *LPCWSTR;
typedef WCHAR              *LPWSTR;
typedef BOOL               *LPBOOL;
typedef DWORD              *LPDWORD;

#define MAX_PATH 260

typedef struct _FILETIME { DWORD dwLowDateTime, dwHighDateTime; } FILETIME;

typedef struct _WIN32_FIND_DATAA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow, dwReserved0, dwReserved1;
    CHAR  cFileName[MAX_PATH];
    CHAR  cAlternateFileName[14];
} WIN32_FIND_DATAA;

typedef struct _WIN32_FIND_DATAW {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow, dwReserved0, dwReserved1;
    WCHAR cFileName[MAX_PATH];
    WCHAR cAlternateFileName[14];
} WIN32_FIND_DATAW;

typedef enum _GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard, GetFileExMaxInfoLevel } GET_FILEEX_INFO_LEVELS;

/* Opaque pass-throughs: layout never inspected here. */
typedef void *LPSECURITY_ATTRIBUTES;
typedef void *PULARGE_INTEGER;
typedef void *LPPROGRESS_ROUTINE;

#define INVALID_HANDLE_VALUE ((HANDLE)(long)-1)

/* ---- the XDK *A entries we forward to (kernel/xapilib exports) ----------- */
extern HANDLE  CreateMutexA(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
extern HANDLE  OpenMutexA(DWORD, BOOL, LPCSTR);
extern HANDLE  CreateEventA(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR);
extern HANDLE  OpenEventA(DWORD, BOOL, LPCSTR);
extern HANDLE  CreateSemaphoreA(LPSECURITY_ATTRIBUTES, LONG, LONG, LPCSTR);
extern HANDLE  OpenSemaphoreA(DWORD, BOOL, LPCSTR);
extern HANDLE  CreateWaitableTimerA(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
extern HANDLE  OpenWaitableTimerA(DWORD, BOOL, LPCSTR);
extern HMODULE LoadLibraryA(LPCSTR);
extern DWORD   GetModuleFileNameA(HMODULE, LPSTR, DWORD);
extern HMODULE GetModuleHandleA(LPCSTR);
extern LPSTR   GetCommandLineA(void);
extern BOOL    GetDiskFreeSpaceExA(LPCSTR, PULARGE_INTEGER, PULARGE_INTEGER, PULARGE_INTEGER);
extern BOOL    CreateDirectoryA(LPCSTR, LPSECURITY_ATTRIBUTES);
extern BOOL    RemoveDirectoryA(LPCSTR);
extern HANDLE  CreateFileA(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
extern BOOL    SetFileAttributesA(LPCSTR, DWORD);
extern DWORD   GetFileAttributesA(LPCSTR);
extern BOOL    GetFileAttributesExA(LPCSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
extern BOOL    DeleteFileA(LPCSTR);
extern HANDLE  FindFirstFileA(LPCSTR, WIN32_FIND_DATAA *);
extern BOOL    FindNextFileA(HANDLE, WIN32_FIND_DATAA *);
extern BOOL    CopyFileA(LPCSTR, LPCSTR, BOOL);
extern BOOL    CopyFileExA(LPCSTR, LPCSTR, LPPROGRESS_ROUTINE, LPVOID, LPBOOL, DWORD);
extern BOOL    MoveFileA(LPCSTR, LPCSTR);
extern BOOL    MoveFileExA(LPCSTR, LPCSTR, DWORD);
extern BOOL    MoveFileWithProgressA(LPCSTR, LPCSTR, LPPROGRESS_ROUTINE, LPVOID, DWORD);
extern BOOL    GetVolumeInformationA(LPCSTR, LPSTR, DWORD, LPDWORD, LPDWORD, LPDWORD, LPSTR, DWORD);

/* ---- ASCII narrow/widen (Xbox paths and object names are ASCII) ---------- */
static void narrow(char *dst, LPCWSTR src, unsigned cap)
{
    unsigned i = 0;
    if (!src) { if (cap) dst[0] = 0; return; }
    for (; src[i] && i + 1 < cap; ++i) dst[i] = (char)src[i];
    dst[i] = 0;
}
static void widen(WCHAR *dst, const char *src, unsigned cap)
{
    unsigned i = 0;
    if (!cap) return;
    if (!src) { dst[0] = 0; return; }
    for (; src[i] && i + 1 < cap; ++i) dst[i] = (unsigned char)src[i];
    dst[i] = 0;
}

/* ---- synchronization objects -------------------------------------------- */
HANDLE CreateMutexW(LPSECURITY_ATTRIBUTES a, BOOL o, LPCWSTR n)
{ char b[MAX_PATH]; narrow(b, n, MAX_PATH); return CreateMutexA(a, o, n ? b : 0); }
HANDLE OpenMutexW(DWORD acc, BOOL inh, LPCWSTR n)
{ char b[MAX_PATH]; narrow(b, n, MAX_PATH); return OpenMutexA(acc, inh, n ? b : 0); }
HANDLE CreateEventW(LPSECURITY_ATTRIBUTES a, BOOL m, BOOL s, LPCWSTR n)
{ char b[MAX_PATH]; narrow(b, n, MAX_PATH); return CreateEventA(a, m, s, n ? b : 0); }
HANDLE OpenEventW(DWORD acc, BOOL inh, LPCWSTR n)
{ char b[MAX_PATH]; narrow(b, n, MAX_PATH); return OpenEventA(acc, inh, n ? b : 0); }
HANDLE CreateSemaphoreW(LPSECURITY_ATTRIBUTES a, LONG i, LONG m, LPCWSTR n)
{ char b[MAX_PATH]; narrow(b, n, MAX_PATH); return CreateSemaphoreA(a, i, m, n ? b : 0); }
HANDLE OpenSemaphoreW(DWORD acc, BOOL inh, LPCWSTR n)
{ char b[MAX_PATH]; narrow(b, n, MAX_PATH); return OpenSemaphoreA(acc, inh, n ? b : 0); }
HANDLE CreateWaitableTimerW(LPSECURITY_ATTRIBUTES a, BOOL m, LPCWSTR n)
{ char b[MAX_PATH]; narrow(b, n, MAX_PATH); return CreateWaitableTimerA(a, m, n ? b : 0); }
HANDLE OpenWaitableTimerW(DWORD acc, BOOL inh, LPCWSTR n)
{ char b[MAX_PATH]; narrow(b, n, MAX_PATH); return OpenWaitableTimerA(acc, inh, n ? b : 0); }

/* ---- module / library --------------------------------------------------- */
HMODULE LoadLibraryW(LPCWSTR f)
{ char b[MAX_PATH]; narrow(b, f, MAX_PATH); return LoadLibraryA(f ? b : 0); }
HMODULE GetModuleHandleW(LPCWSTR n)
{ char b[MAX_PATH]; narrow(b, n, MAX_PATH); return GetModuleHandleA(n ? b : 0); }
DWORD GetModuleFileNameW(HMODULE h, LPWSTR out, DWORD n)
{
    char b[MAX_PATH];
    DWORD cap = n < MAX_PATH ? n : MAX_PATH;
    DWORD r = GetModuleFileNameA(h, b, cap);
    widen(out, b, n);
    return r;
}
LPWSTR GetCommandLineW(void)
{
    static WCHAR buf[1024];
    static int inited;
    if (!inited) { widen(buf, GetCommandLineA(), 1024); inited = 1; }
    return buf;
}

/* ---- files / directories ------------------------------------------------ */
BOOL GetDiskFreeSpaceExW(LPCWSTR d, PULARGE_INTEGER a, PULARGE_INTEGER t, PULARGE_INTEGER f)
{ char b[MAX_PATH]; narrow(b, d, MAX_PATH); return GetDiskFreeSpaceExA(d ? b : 0, a, t, f); }
BOOL CreateDirectoryW(LPCWSTR p, LPSECURITY_ATTRIBUTES s)
{ char b[MAX_PATH]; narrow(b, p, MAX_PATH); return CreateDirectoryA(b, s); }
BOOL RemoveDirectoryW(LPCWSTR p)
{ char b[MAX_PATH]; narrow(b, p, MAX_PATH); return RemoveDirectoryA(b); }
HANDLE CreateFileW(LPCWSTR f, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD attr, HANDLE tmpl)
{ char b[MAX_PATH]; narrow(b, f, MAX_PATH); return CreateFileA(b, acc, share, sa, disp, attr, tmpl); }
BOOL SetFileAttributesW(LPCWSTR f, DWORD attr)
{ char b[MAX_PATH]; narrow(b, f, MAX_PATH); return SetFileAttributesA(b, attr); }
DWORD GetFileAttributesW(LPCWSTR f)
{ char b[MAX_PATH]; narrow(b, f, MAX_PATH); return GetFileAttributesA(b); }
BOOL GetFileAttributesExW(LPCWSTR f, GET_FILEEX_INFO_LEVELS lvl, LPVOID info)
{ char b[MAX_PATH]; narrow(b, f, MAX_PATH); return GetFileAttributesExA(b, lvl, info); }
BOOL DeleteFileW(LPCWSTR f)
{ char b[MAX_PATH]; narrow(b, f, MAX_PATH); return DeleteFileA(b); }

static void copy_find(WIN32_FIND_DATAW *w, const WIN32_FIND_DATAA *a)
{
    w->dwFileAttributes = a->dwFileAttributes;
    w->ftCreationTime = a->ftCreationTime;
    w->ftLastAccessTime = a->ftLastAccessTime;
    w->ftLastWriteTime = a->ftLastWriteTime;
    w->nFileSizeHigh = a->nFileSizeHigh;
    w->nFileSizeLow = a->nFileSizeLow;
    w->dwReserved0 = a->dwReserved0;
    w->dwReserved1 = a->dwReserved1;
    widen(w->cFileName, a->cFileName, MAX_PATH);
    widen(w->cAlternateFileName, a->cAlternateFileName, 14);
}
HANDLE FindFirstFileW(LPCWSTR f, WIN32_FIND_DATAW *out)
{
    char b[MAX_PATH]; WIN32_FIND_DATAA a; HANDLE h;
    narrow(b, f, MAX_PATH);
    h = FindFirstFileA(b, &a);
    if (h != INVALID_HANDLE_VALUE && out) copy_find(out, &a);
    return h;
}
BOOL FindNextFileW(HANDLE h, WIN32_FIND_DATAW *out)
{
    WIN32_FIND_DATAA a;
    BOOL r = FindNextFileA(h, &a);
    if (r && out) copy_find(out, &a);
    return r;
}
BOOL CopyFileW(LPCWSTR ex, LPCWSTR nw, BOOL fail)
{ char a[MAX_PATH], b[MAX_PATH]; narrow(a, ex, MAX_PATH); narrow(b, nw, MAX_PATH); return CopyFileA(a, b, fail); }
BOOL CopyFileExW(LPCWSTR ex, LPCWSTR nw, LPPROGRESS_ROUTINE pr, LPVOID d, LPBOOL cancel, DWORD flags)
{ char a[MAX_PATH], b[MAX_PATH]; narrow(a, ex, MAX_PATH); narrow(b, nw, MAX_PATH); return CopyFileExA(a, b, pr, d, cancel, flags); }
BOOL MoveFileW(LPCWSTR ex, LPCWSTR nw)
{ char a[MAX_PATH], b[MAX_PATH]; narrow(a, ex, MAX_PATH); narrow(b, nw, MAX_PATH); return MoveFileA(a, b); }
BOOL MoveFileExW(LPCWSTR ex, LPCWSTR nw, DWORD flags)
{ char a[MAX_PATH], b[MAX_PATH]; narrow(a, ex, MAX_PATH); narrow(b, nw, MAX_PATH); return MoveFileExA(a, nw ? b : 0, flags); }
BOOL MoveFileWithProgressW(LPCWSTR ex, LPCWSTR nw, LPPROGRESS_ROUTINE pr, LPVOID d, DWORD flags)
{ char a[MAX_PATH], b[MAX_PATH]; narrow(a, ex, MAX_PATH); narrow(b, nw, MAX_PATH); return MoveFileWithProgressA(a, nw ? b : 0, pr, d, flags); }
BOOL GetVolumeInformationW(LPCWSTR root, LPWSTR volBuf, DWORD volSize,
                           LPDWORD serial, LPDWORD compLen, LPDWORD fsFlags,
                           LPWSTR fsBuf, DWORD fsSize)
{
    char rb[MAX_PATH], vb[MAX_PATH], fb[MAX_PATH];
    BOOL r;
    narrow(rb, root, MAX_PATH);
    r = GetVolumeInformationA(root ? rb : 0,
                              volBuf ? vb : 0, volBuf ? MAX_PATH : 0,
                              serial, compLen, fsFlags,
                              fsBuf ? fb : 0, fsBuf ? MAX_PATH : 0);
    if (r) {
        if (volBuf) widen(volBuf, vb, volSize);
        if (fsBuf)  widen(fsBuf, fb, fsSize);
    }
    return r;
}
