/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * RXDK-360 secure-CRT C++ overloads for the modern (clang + picolibc) toolchain.
 *
 * MSVC's CRT provides C++ template overloads of the bounds-checked *_s functions
 * that deduce the destination buffer size from an array argument - e.g.
 * strcpy_s(dst_array, src) instead of strcpy_s(dst, size, src). The stock XDK/ATG
 * code uses these array forms heavily. picolibc supplies the C11 Annex K 3-arg
 * forms (with __STDC_WANT_LIB_EXT1__), but not the C++ array overloads, so those
 * calls fail to resolve. Provide the overloads here, forwarding to the Annex K
 * functions. Force-included in the modern (XDK-headers) build.
 */
#ifndef RXDK_SECURE_OVERLOADS_H
#define RXDK_SECURE_OVERLOADS_H
#if defined(__cplusplus) && defined(__STDC_WANT_LIB_EXT1__)

#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>

/* ---- narrow string ---- */
template <size_t N> inline int strcpy_s(char (&d)[N], const char *s) { return strcpy_s(d, N, s); }
template <size_t N> inline int strcat_s(char (&d)[N], const char *s) { return strcat_s(d, N, s); }
template <size_t N> inline int strncpy_s(char (&d)[N], const char *s, size_t c) { return strncpy_s(d, N, s, c); }
template <size_t N> inline int strncat_s(char (&d)[N], const char *s, size_t c) { return strncat_s(d, N, s, c); }

/* ---- wide string ---- */
template <size_t N> inline int wcscpy_s(wchar_t (&d)[N], const wchar_t *s) { return wcscpy_s(d, N, s); }
template <size_t N> inline int wcscat_s(wchar_t (&d)[N], const wchar_t *s) { return wcscat_s(d, N, s); }
template <size_t N> inline int wcsncpy_s(wchar_t (&d)[N], const wchar_t *s, size_t c) { return wcsncpy_s(d, N, s, c); }

/* ---- memory ---- */
template <size_t N> inline int memcpy_s(char (&d)[N], const void *s, size_t n) { return memcpy_s(d, N, s, n); }
template <size_t N> inline int memmove_s(char (&d)[N], const void *s, size_t n) { return memmove_s(d, N, s, n); }

/* ---- narrow formatted (variadic array forms) ---- */
template <size_t N> inline int sprintf_s(char (&d)[N], const char *fmt, ...)
{ va_list a; va_start(a, fmt); int r = vsnprintf(d, N, fmt, a); va_end(a); return r; }
template <size_t N> inline int vsprintf_s(char (&d)[N], const char *fmt, va_list a)
{ return vsnprintf(d, N, fmt, a); }

/* ---- wide formatted ---- */
template <size_t N> inline int swprintf_s(wchar_t (&d)[N], const wchar_t *fmt, ...)
{ va_list a; va_start(a, fmt); int r = vswprintf(d, N, fmt, a); va_end(a); return r; }
template <size_t N> inline int vswprintf_s(wchar_t (&d)[N], const wchar_t *fmt, va_list a)
{ return vswprintf(d, N, fmt, a); }

/* ---- conversions ---- */
template <size_t N> inline int wcstombs_s(size_t *conv, char (&d)[N], const wchar_t *s, size_t c)
{ return wcstombs_s(conv, d, N, s, c); }
template <size_t N> inline int mbstowcs_s(size_t *conv, wchar_t (&d)[N], const char *s, size_t c)
{ return mbstowcs_s(conv, d, N, s, c); }

#endif /* __cplusplus && __STDC_WANT_LIB_EXT1__ */
#endif /* RXDK_SECURE_OVERLOADS_H */
