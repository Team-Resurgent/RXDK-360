/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * MS CRT compatibility surface: the underscore-spelled and secure (_s) CRT
 * entry points that cl.exe-built XDK libs import from libcMT/libcpMT. Each
 * forwards to the equivalent real function in our modern runtime, so the shipped
 * libs link against ours instead of the MS CRT. See the parity audit in
 * tools/lib_parity.py; this covers the mechanical "C-clean" subset.
 *
 * NOT here (handled elsewhere / deliberately deferred):
 *   - exit()                     -> runtime/xbox/crt_start.c (drives our atexit)
 *   - __savevmx_N and __restvmx_N, __u64tod, _blkmov, __jump_unwind, _RtlCheckStack12
 *                                -> leaf ABI glue, reused from the MS objects
 *                                   (build_libc.py, like crtgpr/crtfpr)
 *   - __iob_func, __onexitbegin/__onexitend
 *                                -> MS FILE / atexit-table ABI, need a layout
 *                                   decision (tracked in the parity memo)
 */
#include <errno.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <wchar.h>
#include <wctype.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- case-insensitive compares (MS spelling -> POSIX) ---- */
int _stricmp(const char *a, const char *b)             { return strcasecmp(a, b); }
int _strnicmp(const char *a, const char *b, size_t n)  { return strncasecmp(a, b, n); }
int _strcmpi(const char *a, const char *b)             { return strcasecmp(a, b); }
int _wcsicmp(const wchar_t *a, const wchar_t *b)            { return wcscasecmp(a, b); }
int _wcsnicmp(const wchar_t *a, const wchar_t *b, size_t n) { return wcsncasecmp(a, b, n); }

/* ---- in-place wide lowercase ---- */
wchar_t *_wcslwr(wchar_t *s) {
    for (wchar_t *p = s; *p; ++p) *p = (wchar_t)towlower(*p);
    return s;
}
int _wcslwr_s(wchar_t *s, size_t n) {            /* errno_t; 0 == success */
    if (!s) return EINVAL;
    for (size_t i = 0; i < n && s[i]; ++i) s[i] = (wchar_t)towlower(s[i]);
    return 0;
}

/* ---- wide string -> number ---- */
double _wtof(const wchar_t *s)  { return wcstod(s, NULL); }
int    _wtoi(const wchar_t *s)  { return (int)wcstol(s, NULL, 10); }
long   _wtol(const wchar_t *s)  { return wcstol(s, NULL, 10); }

/* ---- classic FP classification ---- */
int _finite(double x) { return __builtin_isfinite(x); }
int _isnan(double x)  { return __builtin_isnan(x); }

/* ---- narrow/wide bounded + unbounded printf spellings ---- */
int _snprintf(char *buf, size_t n, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap); return r;
}
int _vsnprintf(char *buf, size_t n, const char *fmt, va_list ap) {
    return vsnprintf(buf, n, fmt, ap);
}
int _snwprintf(wchar_t *buf, size_t n, const wchar_t *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vswprintf(buf, n, fmt, ap);
    va_end(ap); return r;
}
int _vswprintf(wchar_t *buf, const wchar_t *fmt, va_list ap) {   /* old, no size */
    return vswprintf(buf, (size_t)-1, fmt, ap);
}
int _vswprintf_c_l(wchar_t *buf, size_t n, const wchar_t *fmt, void *loc, va_list ap) {
    (void)loc; return vswprintf(buf, n, fmt, ap);               /* locale ignored */
}

/* ---- integer -> string ---- */
char *_itoa(int v, char *buf, int radix) { return itoa(v, buf, radix); }
int _itow_s(int v, wchar_t *buf, size_t n, int radix) {         /* errno_t */
    char tmp[34];
    if (!buf || n == 0) return EINVAL;
    itoa(v, tmp, radix);
    size_t i = 0;
    for (; tmp[i] && i + 1 < n; ++i) buf[i] = (wchar_t)(unsigned char)tmp[i];
    buf[i] = 0;
    return tmp[i] ? ERANGE : 0;
}

/* ---- misc CRT hooks ---- */
int  *_errno(void)   { return &errno; }
int   _purecall(void){ abort(); return 0; }                     /* pure-virtual call */
int   _mtinit(void)  { return 1; }                              /* threads self-init */
void  _RTC_Initialize(void) {}                                  /* RTC checks: no-op */
FILE *_fdopen(int fd, const char *mode) { return fdopen(fd, mode); }

/* run atexit/static-dtor handlers without terminating (crt_start owns the array) */
extern void __rxdk_run_atexit(void);
void _cexit(void) { __rxdk_run_atexit(); }

/* the linker marker cl.exe emits into any object that uses floating point */
int _fltused = 0x9875;

/* wide assertion failure -> trace + abort */
extern int DbgPrint(const char *, ...);
void _wassert(const wchar_t *msg, const wchar_t *file, unsigned line) {
    DbgPrint("assertion failed: %ls (%ls:%u)\n", msg, file, line);
    abort();
}

/* ---- stack security cookie (leaf glue; MS-compiled objects reference these) ---- */
unsigned long __security_cookie = 0xBB40E64EUL;
void __security_check_cookie(unsigned long got) {
    if (got != __security_cookie) abort();                      /* __report_gsfailure */
}

/* ---- Annex-K "secure" wrappers over our real functions ---- */
int fopen_s(FILE **pf, const char *name, const char *mode) {
    if (!pf) return EINVAL;
    *pf = fopen(name, mode);
    return *pf ? 0 : errno;
}
int wcscpy_s(wchar_t *dst, size_t n, const wchar_t *src) {
    if (!dst || !src || n == 0) return EINVAL;
    size_t i = 0;
    for (; src[i] && i + 1 < n; ++i) dst[i] = src[i];
    if (src[i]) { dst[0] = 0; return ERANGE; }
    dst[i] = 0; return 0;
}
int wcsncpy_s(wchar_t *dst, size_t n, const wchar_t *src, size_t cnt) {
    if (!dst || n == 0) return EINVAL;
    size_t i = 0;
    for (; i < cnt && src && src[i] && i + 1 < n; ++i) dst[i] = src[i];
    if (i + 1 > n) { dst[0] = 0; return ERANGE; }
    dst[i] = 0; return 0;
}
int wcscat_s(wchar_t *dst, size_t n, const wchar_t *src) {
    if (!dst || !src) return EINVAL;
    size_t len = wcsnlen(dst, n);
    if (len == n) return EINVAL;                                /* not terminated */
    return wcscpy_s(dst + len, n - len, src);
}
int vsprintf_s(char *buf, size_t n, const char *fmt, va_list ap) {
    return vsnprintf(buf, n, fmt, ap);                          /* bounded */
}
int vswprintf_s(wchar_t *buf, size_t n, const wchar_t *fmt, va_list ap) {
    return vswprintf(buf, n, fmt, ap);
}
/* NOTE: sscanf_s/swscanf_s are NOT thin forwards -- MS passes a buffer-size arg
   after each %s/%c in the varargs, so forwarding to sscanf() would desync the
   argument list. Left for a real secure-scan implementation. */
