/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * RXDK-360 MSVC-CRT extension shims for the modern (clang + picolibc) toolchain.
 *
 * The stock XDK/ATG code uses a handful of Microsoft-specific CRT extensions
 * (the underscore-prefixed names) that ISO C / picolibc do not provide. picolibc
 * does provide the POSIX equivalents, so forward to those. Force-included in the
 * modern build so XDK/sample sources resolve them without the XDK's own CRT.
 */
#ifndef RXDK_MSVCRT_COMPAT_H
#define RXDK_MSVCRT_COMPAT_H

/* ---- macros ---- */
#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef _MAX_PATH
#define _MAX_PATH 260
#endif
#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif
/* SAL code-analysis fallthrough marker: the XDK places `__fallthrough` before a
   `case` label with no trailing statement, where clang's real fallthrough
   attribute is rejected. It carries no codegen meaning - make it a no-op.
   picolibc's <sys/cdefs.h> (force-included first, via picolibc.h) defines this
   as the real [[fallthrough]] attribute for its own build; this header is
   force-included after it, so override that definition to the empty marker the
   XDK's usage needs. */
#undef __fallthrough
#define __fallthrough

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>   /* strcasecmp / strncasecmp */
#include <wchar.h>
#include <wctype.h>
#include <alloca.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- case-insensitive compares ---- */
static inline int _stricmp(const char *a, const char *b) { return strcasecmp(a, b); }
static inline int _strnicmp(const char *a, const char *b, size_t n) { return strncasecmp(a, b, n); }
static inline int _wcsicmp(const wchar_t *a, const wchar_t *b) { return wcscasecmp(a, b); }
static inline int _wcsnicmp(const wchar_t *a, const wchar_t *b, size_t n) { return wcsncasecmp(a, b, n); }

/* ---- wide numeric conversions ---- */
static inline int       _wtoi(const wchar_t *s)   { return (int)wcstol(s, (wchar_t **)0, 10); }
static inline long long _wtoi64(const wchar_t *s) { return wcstoll(s, (wchar_t **)0, 10); }
static inline double    _wtof(const wchar_t *s)   { return wcstod(s, (wchar_t **)0); }

/* ---- format-length queries ---- */
static inline int _vscprintf(const char *fmt, va_list a) { return vsnprintf((char *)0, 0, fmt, a); }
static inline int _vscwprintf(const wchar_t *fmt, va_list a)
{ wchar_t tmp[4096]; return vswprintf(tmp, sizeof(tmp) / sizeof(tmp[0]), fmt, a); }

/* ---- bounded printf (MS spelling) ---- */
static inline int _vsnprintf_s(char *d, size_t dstsize, size_t count, const char *fmt, va_list a)
{ (void)count; return vsnprintf(d, dstsize, fmt, a); }

/* ---- lowercase in place ---- */
static inline int _wcslwr_s(wchar_t *s, size_t size)
{ size_t i; for (i = 0; i < size && s[i]; ++i) s[i] = (wchar_t)towlower(s[i]); return 0; }

/* ---- integer to ASCII (base 10 and 16 cover XDK use) ---- */
static inline int _itoa_s(int val, char *buf, size_t size, int radix)
{ if (radix == 16) return snprintf(buf, size, "%x", (unsigned)val) < 0 ? 22 : 0;
  return snprintf(buf, size, "%d", val) < 0 ? 22 : 0; }

#ifdef __cplusplus
}
/* MSVC array-size-deducing overloads (C++ only). */
template <size_t N> inline int _wcslwr_s(wchar_t (&s)[N]) { return _wcslwr_s(s, N); }
template <size_t N> inline int _itoa_s(int val, char (&buf)[N], int radix) { return _itoa_s(val, buf, N, radix); }
/* _vsnprintf_s(buf, count, fmt, ap): buffer size deduced; count is usually
   _TRUNCATE (truncate to fit). Forward to vsnprintf bounded by the array size. */
template <size_t N> inline int _vsnprintf_s(char (&d)[N], size_t count, const char *fmt, va_list a)
{ (void)count; return vsnprintf(d, N, fmt, a); }
template <size_t N> inline int _vsnwprintf_s(wchar_t (&d)[N], size_t count, const wchar_t *fmt, va_list a)
{ (void)count; return vswprintf(d, N, fmt, a); }
#endif

/* ---- stack/heap alloc helpers. _malloca/_freea are paired; use the heap to
 *      avoid blowing the (small) title stack on large sizes. ---- */
#ifndef _alloca
#define _alloca(n) alloca(n)
#endif
#ifndef _malloca
#define _malloca(n) malloc(n)
#endif
#ifndef _freea
#define _freea(p) free(p)
#endif

#endif /* RXDK_MSVCRT_COMPAT_H */
