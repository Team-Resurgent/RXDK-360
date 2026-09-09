/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * MSVC printf conversions (%S, %C, and MSVC's reading of %s/%c in the wide
 * functions).
 *
 * MSVC and C99 disagree about which conversion means "wide":
 *
 *              MSVC                         C99
 *   narrow     %s narrow, %S wide           %s narrow, %ls wide
 *   wide       %s wide,   %S narrow         %s narrow, %ls wide
 *
 * So in both families C99 says "l means wide", while MSVC says "%S is whatever
 * the function is not" and lets a bare %s follow the function's own width.
 * XDK-era title code is written to the MSVC rule and picolibc implements the
 * C99 one; an unrecognised conversion is echoed verbatim, so a path built with
 * sprintf(buf, "d:\\%S", L"Media\\") came out as the literal text "d:\%S".
 *
 * Rather than patch the vendored picolibc engine, the entry points that titles
 * actually reach translate the format first. sprintf and swprintf live here
 * because picolibc's versions are excluded from the build in libs/libc/build.zig;
 * the MSVC-spelled _vsnprintf and _snwprintf translate in msvc_crt.c.
 *
 * MSVC's %I64/%I32 size prefixes are respelled here too: picolibc knows only the
 * C99 length modifiers, so "%I64d" came out as the literal text "%I64d".
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "ms_printf.h"
#include "local-stdio.h" /* __file_str + FDEV_SETUP_STRING_WRITE: the bounded core */

#ifndef __cdecl
#define __cdecl
#endif

static int ms_is_flag(unsigned c)
{
    return c == '-' || c == '+' || c == ' ' || c == '#' || c == '0' || c == '\'';
}

static int ms_is_width(unsigned c)
{
    return (c >= '0' && c <= '9') || c == '*';
}

static int ms_is_length(unsigned c)
{
    return c == 'h' || c == 'l' || c == 'L' || c == 'j' || c == 'z' || c == 't' || c == 'w';
}

/*
 * Map one conversion to its C99 spelling, as ASCII, given the length modifier
 * exactly as written and whether this is a wide printf. Both families end up
 * using the same rule -- 'l' means wide -- which is why one mapping serves the
 * narrow and wide translators alike.
 */
static void ms_map_conv(const char *mod, unsigned conv, int wide, char *out)
{
    int arg_is_wide;
    size_t i = 0;

    switch (conv) {
    case 'S':
    case 'C':
        /* MSVC's uppercase pair always means the opposite of the function. */
        arg_is_wide = !wide;
        break;
    case 's':
    case 'c':
        if (mod[0] == 'h') {
            arg_is_wide = 0;
        } else if (mod[0] == 'l' || mod[0] == 'w') {
            arg_is_wide = 1;
        } else {
            arg_is_wide = wide;
        }
        break;
    default:
        /* Not a string or character conversion, so only MSVC's 'w' modifier
         * needs respelling. */
        for (; *mod; mod++) {
            out[i++] = (*mod == 'w') ? 'l' : *mod;
        }
        out[i++] = (char)conv;
        out[i] = '\0';
        return;
    }

    if (arg_is_wide) {
        out[i++] = 'l';
    }
    out[i++] = (conv == 'S' || conv == 's') ? 's' : 'c';
    out[i] = '\0';
}

/*
 * The two translators below are the same walk over a format string, differing
 * only in character type; keep them in step. Worst case a directive grows by
 * one character (%S -> %ls), so twice the input always fits.
 */

#define MS_TRANSLATE_BODY(CH)                                                 \
    size_t in = 0, out = 0;                                                   \
    char mod[8], repl[8];                                                     \
    size_t n;                                                                 \
                                                                              \
    while (fmt[in]) {                                                         \
        if (fmt[in] != '%') {                                                 \
            dst[out++] = fmt[in++];                                           \
            continue;                                                         \
        }                                                                     \
        dst[out++] = fmt[in++];                                               \
        if (fmt[in] == '%') {                                                 \
            dst[out++] = fmt[in++];                                           \
            continue;                                                         \
        }                                                                     \
        while (fmt[in] && ms_is_flag((unsigned)fmt[in]))                      \
            dst[out++] = fmt[in++];                                           \
        while (fmt[in] && ms_is_width((unsigned)fmt[in]))                     \
            dst[out++] = fmt[in++];                                           \
        if (fmt[in] == '.') {                                                 \
            dst[out++] = fmt[in++];                                           \
            while (fmt[in] && ms_is_width((unsigned)fmt[in]))                 \
                dst[out++] = fmt[in++];                                       \
        }                                                                     \
        n = 0;                                                                \
        /* MSVC spells the 64- and 32-bit sizes %I64 and %I32; here long long  \
         * is the 64-bit type and int is already 32-bit. */                    \
        if (fmt[in] == 'I' && fmt[in + 1] == '6' && fmt[in + 2] == '4') {      \
            in += 3;                                                          \
            mod[n++] = 'l';                                                   \
            mod[n++] = 'l';                                                   \
        } else if (fmt[in] == 'I' && fmt[in + 1] == '3' &&                     \
                   fmt[in + 2] == '2') {                                      \
            in += 3;                                                          \
        } else {                                                              \
            while (n < 3 && fmt[in] && ms_is_length((unsigned)fmt[in]))        \
                mod[n++] = (char)fmt[in++];                                   \
        }                                                                     \
        mod[n] = '\0';                                                        \
        if (!fmt[in]) {                                                       \
            /* Truncated directive: emit what was written and stop. */        \
            for (n = 0; mod[n]; n++)                                          \
                dst[out++] = (CH)mod[n];                                      \
            break;                                                            \
        }                                                                     \
        ms_map_conv(mod, (unsigned)fmt[in++], wide, repl);                    \
        for (n = 0; repl[n]; n++)                                             \
            dst[out++] = (CH)repl[n];                                         \
    }                                                                         \
    dst[out] = 0;

static void ms_translate(const char *fmt, char *dst, int wide)
{
    MS_TRANSLATE_BODY(char)
}

static void ms_translate_w(const wchar_t *fmt, wchar_t *dst, int wide)
{
    MS_TRANSLATE_BODY(wchar_t)
}

const char *__rxdk_ms_format(const char *fmt, char *buf, size_t cap, char **heap)
{
    size_t len = 0;
    char *dst = buf;

    *heap = NULL;
    if (!fmt) {
        return fmt;
    }
    while (fmt[len]) {
        len++;
    }
    if (len == 0) {
        return fmt;
    }

    if (len * 2 + 1 > cap) {
        dst = (char *)malloc(len * 2 + 1);
        if (!dst) {
            return fmt;
        }
        *heap = dst;
    }

    ms_translate(fmt, dst, 0);
    return dst;
}

const wchar_t *__rxdk_ms_wformat(const wchar_t *fmt, wchar_t *buf, size_t cap, wchar_t **heap)
{
    size_t len = 0;
    wchar_t *dst = buf;

    *heap = NULL;
    if (!fmt) {
        return fmt;
    }
    while (fmt[len]) {
        len++;
    }
    if (len == 0) {
        return fmt;
    }

    if (len * 2 + 1 > cap) {
        dst = (wchar_t *)malloc((len * 2 + 1) * sizeof(wchar_t));
        if (!dst) {
            return fmt;
        }
        *heap = dst;
    }

    ms_translate_w(fmt, dst, 1);
    return dst;
}

void __rxdk_ms_format_free(void *heap)
{
    if (heap) {
        free(heap);
    }
}

int __cdecl sprintf(char *s, const char *fmt, ...)
{
    char stack[RXDK_MS_FORMAT_STACK];
    char *heap;
    const char *use = __rxdk_ms_format(fmt, stack, sizeof(stack), &heap);
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = vsprintf(s, use, ap);
    va_end(ap);

    __rxdk_ms_format_free(heap);
    return r;
}

int __cdecl swprintf(wchar_t *s, size_t n, const wchar_t *fmt, ...)
{
    wchar_t stack[RXDK_MS_FORMAT_STACK];
    wchar_t *heap;
    const wchar_t *use = __rxdk_ms_wformat(fmt, stack, RXDK_MS_FORMAT_STACK, &heap);
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = vswprintf(s, n, use, ap);
    va_end(ap);

    __rxdk_ms_format_free(heap);
    return r;
}

/*
 * The bounded narrow printf family. picolibc's snprintf/vsnprintf are excluded
 * from the build (libs/libc/build.zig) in favour of these so that a bare
 * snprintf(buf, n, "%S", L"...") -- which is exactly what SDL's SDL_vsnprintf
 * reaches when HAVE_VSNPRINTF is set, and what any ported MSVC code expects --
 * gets the same %S/%C/%I64 translation that sprintf already applies. The engine
 * is picolibc's own: format into a bounded string FILE via vfprintf.
 */
int __rxdk_vsnprintf_core(char *s, size_t n, const char *fmt, va_list ap)
{
    struct __file_str f = FDEV_SETUP_STRING_WRITE(s, FDEV_STRING_WRITE_END(s, n));
    int i = vfprintf(&f.file, fmt, ap);

    if (n) {
        *f.pos = '\0';
    }
    return i;
}

int __cdecl vsnprintf(char *s, size_t n, const char *fmt, va_list ap)
{
    char stack[RXDK_MS_FORMAT_STACK];
    char *heap;
    const char *use = __rxdk_ms_format(fmt, stack, sizeof(stack), &heap);
    int i = __rxdk_vsnprintf_core(s, n, use, ap);

    __rxdk_ms_format_free(heap);
    return i;
}

int __cdecl snprintf(char *s, size_t n, const char *fmt, ...)
{
    va_list ap;
    int i;

    va_start(ap, fmt);
    i = vsnprintf(s, n, fmt, ap); /* translation happens once, in vsnprintf */
    va_end(ap);
    return i;
}
