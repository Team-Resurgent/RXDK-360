/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * RXDK-360 secure-CRT (Annex K / MSVC *_s) support for the modern toolchain.
 *
 * picolibc provides the *narrow* C11 Annex K bounds-checked functions (strcpy_s,
 * strcat_s, sprintf_s, ...) when __STDC_WANT_LIB_EXT1__ is set, but NOT the wide
 * (wcs*_s) family, swscanf_s, or the MSVC C++ array-size-deducing overloads that
 * the stock XDK/ATG code uses. Provide the missing wide functions with faithful
 * Annex K semantics, plus the C++ array overloads for both widths, all forwarding
 * to the modern runtime. Force-included in the modern build.
 */
#ifndef RXDK_SECURE_OVERLOADS_H
#define RXDK_SECURE_OVERLOADS_H
#if defined(__cplusplus) && defined(__STDC_WANT_LIB_EXT1__)

#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>

#ifndef RXDK_EINVAL
#define RXDK_EINVAL 22
#define RXDK_ERANGE 34
#endif

/* ---- wide Annex K bases (picolibc lacks these) ---------------------------- */
static inline int wcscpy_s(wchar_t *d, size_t n, const wchar_t *s)
{
    if (!d || !s || n == 0) { if (d && n) d[0] = 0; return RXDK_EINVAL; }
    size_t i = 0; for (; s[i]; ++i) { if (i >= n) { d[0] = 0; return RXDK_ERANGE; } d[i] = s[i]; }
    if (i >= n) { d[0] = 0; return RXDK_ERANGE; } d[i] = 0; return 0;
}
static inline int wcsncpy_s(wchar_t *d, size_t n, const wchar_t *s, size_t cnt)
{
    if (!d || !s || n == 0) { if (d && n) d[0] = 0; return RXDK_EINVAL; }
    size_t i = 0; for (; i < cnt && s[i]; ++i) { if (i >= n) { d[0] = 0; return RXDK_ERANGE; } d[i] = s[i]; }
    if (i >= n) { d[0] = 0; return RXDK_ERANGE; } d[i] = 0; return 0;
}
static inline int wcscat_s(wchar_t *d, size_t n, const wchar_t *s)
{
    if (!d || !s || n == 0) return RXDK_EINVAL;
    size_t l = 0; while (l < n && d[l]) ++l; if (l >= n) { d[0] = 0; return RXDK_EINVAL; }
    size_t i = 0; for (; s[i]; ++i) { if (l + i >= n) { d[0] = 0; return RXDK_ERANGE; } d[l + i] = s[i]; }
    if (l + i >= n) { d[0] = 0; return RXDK_ERANGE; } d[l + i] = 0; return 0;
}
static inline int wcsncat_s(wchar_t *d, size_t n, const wchar_t *s, size_t cnt)
{
    if (!d || !s || n == 0) return RXDK_EINVAL;
    size_t l = 0; while (l < n && d[l]) ++l; if (l >= n) { d[0] = 0; return RXDK_EINVAL; }
    size_t i = 0; for (; i < cnt && s[i]; ++i) { if (l + i >= n) { d[0] = 0; return RXDK_ERANGE; } d[l + i] = s[i]; }
    if (l + i >= n) { d[0] = 0; return RXDK_ERANGE; } d[l + i] = 0; return 0;
}
static inline int vswprintf_s(wchar_t *d, size_t n, const wchar_t *fmt, va_list a) { return vswprintf(d, n, fmt, a); }
static inline int vsprintf_s(char *d, size_t n, const char *fmt, va_list a) { return vsnprintf(d, n, fmt, a); }

/* swscanf_s: forward to vswscanf (the XDK/ATG uses are numeric formats with no
   size arguments, so the Annex K size parameters do not appear). */
inline int swscanf_s(const wchar_t *s, const wchar_t *fmt, ...)
{ va_list a; va_start(a, fmt); int r = vswscanf(s, fmt, a); va_end(a); return r; }
inline int sscanf_s(const char *s, const char *fmt, ...)
{ va_list a; va_start(a, fmt); int r = vsscanf(s, fmt, a); va_end(a); return r; }

/* ---- MSVC C++ array-size-deducing overloads (both widths) ----------------- */
template <size_t N> inline int strcpy_s(char (&d)[N], const char *s) { return strcpy_s(d, N, s); }
template <size_t N> inline int strcat_s(char (&d)[N], const char *s) { return strcat_s(d, N, s); }
template <size_t N> inline int strncpy_s(char (&d)[N], const char *s, size_t c) { return strncpy_s(d, N, s, c); }
template <size_t N> inline int strncat_s(char (&d)[N], const char *s, size_t c) { return strncat_s(d, N, s, c); }
template <size_t N> inline int wcscpy_s(wchar_t (&d)[N], const wchar_t *s) { return wcscpy_s(d, N, s); }
template <size_t N> inline int wcscat_s(wchar_t (&d)[N], const wchar_t *s) { return wcscat_s(d, N, s); }
template <size_t N> inline int wcsncpy_s(wchar_t (&d)[N], const wchar_t *s, size_t c) { return wcsncpy_s(d, N, s, c); }
template <size_t N> inline int wcsncat_s(wchar_t (&d)[N], const wchar_t *s, size_t c) { return wcsncat_s(d, N, s, c); }

/* variadic array forms */
template <size_t N> inline int sprintf_s(char (&d)[N], const char *fmt, ...)
{ va_list a; va_start(a, fmt); int r = vsnprintf(d, N, fmt, a); va_end(a); return r; }
template <size_t N> inline int swprintf_s(wchar_t (&d)[N], const wchar_t *fmt, ...)
{ va_list a; va_start(a, fmt); int r = vswprintf(d, N, fmt, a); va_end(a); return r; }
template <size_t N> inline int vsprintf_s(char (&d)[N], const char *fmt, va_list a) { return vsnprintf(d, N, fmt, a); }
template <size_t N> inline int vswprintf_s(wchar_t (&d)[N], const wchar_t *fmt, va_list a) { return vswprintf(d, N, fmt, a); }

/* explicit-size forms: swprintf_s(buf, count, fmt, ...) as many samples call it */
static inline int sprintf_s(char *d, size_t n, const char *fmt, ...)
{ va_list a; va_start(a, fmt); int r = vsnprintf(d, n, fmt, a); va_end(a); return r; }
static inline int swprintf_s(wchar_t *d, size_t n, const wchar_t *fmt, ...)
{ va_list a; va_start(a, fmt); int r = vswprintf(d, n, fmt, a); va_end(a); return r; }

#endif /* __cplusplus && __STDC_WANT_LIB_EXT1__ */
#endif /* RXDK_SECURE_OVERLOADS_H */
