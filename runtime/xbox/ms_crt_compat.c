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
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* ---- case-insensitive compares (MS spelling -> POSIX) ---- */
int _stricmp(const char *a, const char *b)             { return strcasecmp(a, b); }
int _strnicmp(const char *a, const char *b, size_t n)  { return strncasecmp(a, b, n); }
int _strcmpi(const char *a, const char *b)             { return strcasecmp(a, b); }
/* bare (non-underscore) spellings -- MS ships these in oldnames.lib as aliases to
   the underscore forms; shipped libs import them directly. */
int stricmp(const char *a, const char *b)              { return strcasecmp(a, b); }
int strnicmp(const char *a, const char *b, size_t n)   { return strncasecmp(a, b, n); }
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

/* MS CRT atexit-table bounds (xapilib and the MS CRT init reference these). Our
   real teardown is __cxa_atexit + __rxdk_run_atexit (crt_start.c), not this
   table, so define them as empty bounds for link-completeness; nothing walks
   them. A C library registering via _onexit would go through our path instead. */
typedef void (*_PVFV)(void);
_PVFV *__onexitbegin = (_PVFV *)0;
_PVFV *__onexitend   = (_PVFV *)0;

/* MS internal block move (dst, src, count). Its libcMT member also defines
   memcpy, which would collide with ours, so provide it here over memmove
   (overlap-safe -- the conservative choice for an internal block mover). */
void *_blkmov(void *dst, const void *src, size_t n) { return memmove(dst, src, n); }

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
/* Secure scan: MS passes a buffer-size argument after each %s/%c/%[ pointer, so
   sscanf_s cannot forward to sscanf() directly. Instead scan ONE conversion at a
   time: build a mini-format for the conversion plus a trailing %n, run the real
   sscanf() on the current input position, and advance by the reported count. For
   %s/%c/%[ the size arg is turned into a field width so the buffer can't
   overflow. Suppressed (%*) conversions consume input but take no argument. */
static int rxdk_sscanf_s(const char *in, const char *fmt, va_list ap) {
    int assigned = 0;
    const char *f = fmt;
    while (*f) {
        if (isspace((unsigned char)*f)) { f++; continue; }   /* ws: absorbed by the next conversion */
        if (*f != '%') {                                      /* literal must match */
            while (isspace((unsigned char)*in)) in++;
            if (*in != *f) break;
            in++; f++; continue;
        }
        const char *spec = f++;                               /* spec[0]=='%' */
        int suppress = 0;
        if (*f == '*') { suppress = 1; f++; }
        char wbuf[8]; int wi = 0;
        while (isdigit((unsigned char)*f) && wi < 6) wbuf[wi++] = *f++;
        while (*f=='h'||*f=='l'||*f=='L'||*f=='j'||*f=='z'||*f=='t') f++;
        char conv = *f;
        const char *setstart = 0;
        if (conv == '[') {
            setstart = f; f++;
            if (*f == '^') f++;
            if (*f == ']') f++;
            while (*f && *f != ']') f++;
            if (*f == ']') f++;
        } else if (conv) f++;
        if (conv == '%') { while (isspace((unsigned char)*in)) in++; if (*in=='%') in++; else break; continue; }
        if (!conv) break;

        char mini[80]; int consumed = 0, r; int is_str = (conv=='s'||conv=='c'||conv=='[');
        if (is_str && !suppress) {
            void *buf = va_arg(ap, void *);
            size_t rsize = va_arg(ap, size_t);
            int width = (conv=='c') ? (wi ? atoi(wbuf) : 1) : (int)(rsize ? rsize - 1 : 0);
            if (conv=='c' && (size_t)width > rsize) width = (int)rsize;
            if (width < 0) width = 0;
            int n;
            if (conv=='[') n = snprintf(mini, sizeof mini, "%%%d%.*s%%n", width, (int)(f - setstart), setstart);
            else           n = snprintf(mini, sizeof mini, "%%%d%c%%n", width, conv);
            if (n < 0 || n >= (int)sizeof mini) break;
            r = sscanf(in, mini, buf, &consumed);
            if (r < 1) break;
            in += consumed; assigned++;
        } else {
            int slen = (int)(f - spec);
            if (slen > 60) break;
            memcpy(mini, spec, slen); mini[slen] = 0; strcat(mini, "%n");
            if (suppress) { r = sscanf(in, mini, &consumed); if (consumed == 0) break; in += consumed; }
            else {
                void *arg = va_arg(ap, void *);
                r = sscanf(in, mini, arg, &consumed);
                if (r < 1) break;
                in += consumed; assigned++;
            }
        }
    }
    return assigned;
}
int sscanf_s(const char *in, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = rxdk_sscanf_s(in, fmt, ap);
    va_end(ap); return r;
}
/* wide mirror: identical control flow over swscanf(); the mini-format is built
   narrow (format chars are ASCII) and widened before the swscanf call. */
static int rxdk_swscanf_s(const wchar_t *in, const wchar_t *fmt, va_list ap) {
    int assigned = 0;
    const wchar_t *f = fmt;
    while (*f) {
        if (iswspace(*f)) { f++; continue; }
        if (*f != L'%') { while (iswspace(*in)) in++; if (*in != *f) break; in++; f++; continue; }
        const wchar_t *spec = f++;
        int suppress = 0;
        if (*f == L'*') { suppress = 1; f++; }
        char wbuf[8]; int wi = 0, narrow = 0;
        while (*f >= L'0' && *f <= L'9' && wi < 6) wbuf[wi++] = (char)*f++;
        while (*f==L'h'||*f==L'l'||*f==L'L'||*f==L'j'||*f==L'z'||*f==L't') { if (*f==L'h') narrow = 1; f++; }
        wchar_t conv = *f;
        const wchar_t *setstart = 0;
        if (conv == L'[') { setstart = f; f++; if (*f==L'^') f++; if (*f==L']') f++; while (*f && *f != L']') f++; if (*f==L']') f++; }
        else if (conv) f++;
        if (conv == L'%') { while (iswspace(*in)) in++; if (*in==L'%') in++; else break; continue; }
        if (!conv) break;

        wchar_t mini[80]; int consumed = 0, r; int is_str = (conv==L's'||conv==L'c'||conv==L'[');
        if (is_str && !suppress) {
            void *buf = va_arg(ap, void *);
            size_t rsize = va_arg(ap, size_t);
            int width = (conv==L'c') ? (wi ? atoi(wbuf) : 1) : (int)(rsize ? rsize - 1 : 0);
            if (conv==L'c' && (size_t)width > rsize) width = (int)rsize;
            if (width < 0) width = 0;
            /* build "%<width><conv-or-set>%n" as wide */
            int m = 0; mini[m++] = L'%';
            { char tmp[24]; int tn = snprintf(tmp, sizeof tmp, "%d", width);
              for (int i = 0; i < tn; ++i) mini[m++] = (wchar_t)(unsigned char)tmp[i]; }
            /* MS wide scanf treats %s/%c/%[ as WIDE by default; our standard
               swscanf needs the 'l' modifier for that (bare %s is a char*). */
            if (!narrow) mini[m++] = L'l';
            if (conv==L'[') { for (const wchar_t *s = setstart; s < f && m < 74; ++s) mini[m++] = *s; }
            else mini[m++] = conv;
            mini[m++] = L'%'; mini[m++] = L'n'; mini[m] = 0;
            r = swscanf(in, mini, buf, &consumed);
            if (r < 1) break;
            in += consumed; assigned++;
        } else {
            int slen = (int)(f - spec);
            if (slen > 60) break;
            wmemcpy(mini, spec, slen); mini[slen] = L'%'; mini[slen+1] = L'n'; mini[slen+2] = 0;
            if (suppress) { r = swscanf(in, mini, &consumed); if (consumed == 0) break; in += consumed; }
            else { void *arg = va_arg(ap, void *); r = swscanf(in, mini, arg, &consumed); if (r < 1) break; in += consumed; assigned++; }
        }
    }
    return assigned;
}
int swscanf_s(const wchar_t *in, const wchar_t *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = rxdk_swscanf_s(in, fmt, ap);
    va_end(ap); return r;
}

/* ====================================================================== */
/* Second-wave CRT surface: the underscore CRT/POSIX spellings the shipped */
/* XDK libs import once operator new was satisfied. All forward to our real */
/* implementations or are small self-contained helpers.                    */
/* ====================================================================== */

/* ---- case-insensitive memory compare ---- */
int _memicmp(const void *a, const void *b, size_t n) {
    const unsigned char *p = a, *q = b;
    for (size_t i = 0; i < n; ++i) {
        int d = tolower(p[i]) - tolower(q[i]);
        if (d) return d;
    }
    return 0;
}

/* ---- misc string ---- */
char *_strdup(const char *s)                       { return strdup(s); }
void  _swab(char *src, char *dst, int n) {          /* swap adjacent byte pairs */
    for (n &= ~1; n > 0; n -= 2, src += 2, dst += 2) { dst[0] = src[1]; dst[1] = src[0]; }
}
char *strtok_s(char *s, const char *delim, char **ctx) { return strtok_r(s, delim, ctx); }
wchar_t *wcstok_s(wchar_t *s, const wchar_t *delim, wchar_t **ctx) { return wcstok(s, delim, ctx); }

/* ---- integer -> string (MS radix spellings) ---- */
static char *u2s(unsigned long long v, char *buf, int radix, int neg) {
    static const char D[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char tmp[65]; int i = 0;
    if (radix < 2 || radix > 36) { buf[0] = 0; return buf; }
    do { tmp[i++] = D[v % (unsigned)radix]; v /= (unsigned)radix; } while (v);
    char *p = buf;
    if (neg) *p++ = '-';
    while (i) *p++ = tmp[--i];
    *p = 0;
    return buf;
}
char *_ltoa(long v, char *b, int r)              { return (r == 10 && v < 0) ? u2s((unsigned long long)(-(long long)v), b, r, 1) : u2s((unsigned long)v, b, r, 0); }
char *_ultoa(unsigned long v, char *b, int r)    { return u2s(v, b, r, 0); }
char *_i64toa(long long v, char *b, int r)       { return (r == 10 && v < 0) ? u2s((unsigned long long)(-v), b, r, 1) : u2s((unsigned long long)v, b, r, 0); }
char *_ui64toa(unsigned long long v, char *b, int r) { return u2s(v, b, r, 0); }
static int u2s_s(unsigned long long v, char *b, size_t n, int radix, int neg) {
    char tmp[66]; u2s(v, tmp, radix, neg);
    size_t len = strlen(tmp);
    if (!b || n == 0) return EINVAL;
    if (len + 1 > n) { b[0] = 0; return ERANGE; }
    memcpy(b, tmp, len + 1);
    return 0;
}
int _itoa_s(int v, char *b, size_t n, int r)                  { return u2s_s(r == 10 && v < 0 ? (unsigned long long)(-(long long)v) : (unsigned)v, b, n, r, r == 10 && v < 0); }
int _ui64toa_s(unsigned long long v, char *b, size_t n, int r){ return u2s_s(v, b, n, r, 0); }
/* wide variants: format narrow then widen */
static wchar_t *u2ws(unsigned long long v, wchar_t *b, int radix, int neg) {
    char tmp[66]; u2s(v, tmp, radix, neg);
    wchar_t *p = b; for (char *q = tmp; *q; ++q) *p++ = (wchar_t)(unsigned char)*q; *p = 0; return b;
}
wchar_t *_ltow(long v, wchar_t *b, int r)             { return (r == 10 && v < 0) ? u2ws((unsigned long long)(-(long long)v), b, r, 1) : u2ws((unsigned long)v, b, r, 0); }
wchar_t *_ultow(unsigned long v, wchar_t *b, int r)   { return u2ws(v, b, r, 0); }
wchar_t *_i64tow(long long v, wchar_t *b, int r)      { return (r == 10 && v < 0) ? u2ws((unsigned long long)(-v), b, r, 1) : u2ws((unsigned long long)v, b, r, 0); }
wchar_t *_ui64tow(unsigned long long v, wchar_t *b, int r) { return u2ws(v, b, r, 0); }
int _ultow_s(unsigned long v, wchar_t *b, size_t n, int r) {
    wchar_t tmp[66]; u2ws(v, tmp, r, 0);
    size_t len = wcslen(tmp);
    if (!b || n == 0) return EINVAL;
    if (len + 1 > n) { b[0] = 0; return ERANGE; }
    for (size_t i = 0; i <= len; ++i) b[i] = tmp[i];
    return 0;
}

/* ---- string -> integer (64-bit + unsigned spellings) ---- */
long long          _atoi64(const char *s)                         { return strtoll(s, NULL, 10); }
long long          _strtoi64(const char *s, char **e, int base)   { return strtoll(s, e, base); }
unsigned long long _strtoui64(const char *s, char **e, int base)  { return strtoull(s, e, base); }
long long          _wtoi64(const wchar_t *s)                      { return wcstoll(s, NULL, 10); }
long long          _wcstoi64(const wchar_t *s, wchar_t **e, int base)  { return wcstoll(s, e, base); }
unsigned long long _wcstoui64(const wchar_t *s, wchar_t **e, int base) { return wcstoull(s, e, base); }
long long          _abs64(long long v)                            { return v < 0 ? -v : v; }

/* ---- floating-point classification / control ---- */
/* _fpclass() return bits (MS <float.h>) */
#define _FPCLASS_SNAN 0x0001
#define _FPCLASS_QNAN 0x0002
#define _FPCLASS_NINF 0x0004
#define _FPCLASS_NN   0x0008
#define _FPCLASS_ND   0x0010
#define _FPCLASS_NZ   0x0020
#define _FPCLASS_PZ   0x0040
#define _FPCLASS_PD   0x0080
#define _FPCLASS_PN   0x0100
#define _FPCLASS_PINF 0x0200
int _fpclass(double x) {
    int neg = __builtin_signbit(x);
    if (__builtin_isnan(x)) return _FPCLASS_QNAN;
    if (__builtin_isinf(x)) return neg ? _FPCLASS_NINF : _FPCLASS_PINF;
    if (x == 0.0)           return neg ? _FPCLASS_NZ : _FPCLASS_PZ;
    if (!__builtin_isnormal(x)) return neg ? _FPCLASS_ND : _FPCLASS_PD;   /* subnormal */
    return neg ? _FPCLASS_NN : _FPCLASS_PN;
}
double _chgsign(double x) {
    union { double d; unsigned long long u; } u = { x };
    u.u ^= 1ULL << 63;
    return u.d;
}
double _copysign(double x, double y) { return __builtin_copysign(x, y); }
/* FP status/control word: a title runs with the default word; report it and
   accept changes as no-ops (the console FPSCR is not exposed through fenv here). */
#define _RXDK_FPCW_DEFAULT 0x0009001fu
unsigned _clearfp(void)                                   { return 0; }
unsigned _controlfp(unsigned newv, unsigned mask)         { (void)newv; (void)mask; return _RXDK_FPCW_DEFAULT; }
int      _controlfp_s(unsigned *cur, unsigned newv, unsigned mask) { (void)newv; (void)mask; if (cur) *cur = _RXDK_FPCW_DEFAULT; return 0; }
int      _callnewh(size_t n)                              { (void)n; return 0; }   /* no new-handler installed */

/* ---- extra printf spellings ---- */
int _vsnprintf_s(char *buf, size_t n, size_t count, const char *fmt, va_list ap) {
    (void)count; return vsnprintf(buf, n, fmt, ap);
}
int _vsnwprintf(wchar_t *buf, size_t n, const wchar_t *fmt, va_list ap) {
    return vswprintf(buf, n, fmt, ap);
}
int _vscprintf(const char *fmt, va_list ap) { return vsnprintf(NULL, 0, fmt, ap); }
int swprintf_s(wchar_t *buf, size_t n, const wchar_t *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vswprintf(buf, n, fmt, ap);
    va_end(ap); return r;
}

/* ---- 64-bit time spellings (our time_t already covers the range) ---- */
long long _time64(long long *t)                     { time_t r = time(NULL); if (t) *t = (long long)r; return (long long)r; }
struct tm *_gmtime64(const long long *t)            { time_t tt = (time_t)*t; return gmtime(&tt); }
struct tm *_localtime64(const long long *t)         { time_t tt = (time_t)*t; return localtime(&tt); }
int _localtime64_s(struct tm *tm, const long long *t) { if (!tm || !t) return EINVAL; time_t tt = (time_t)*t; struct tm *r = localtime(&tt); if (!r) return EINVAL; *tm = *r; return 0; }
void _strdate(char *buf) { time_t t = time(NULL); struct tm *m = localtime(&t); if (m) strftime(buf, 9, "%m/%d/%y", m); else buf[0] = 0; }
void _strtime(char *buf) { time_t t = time(NULL); struct tm *m = localtime(&t); if (m) strftime(buf, 9, "%H:%M:%S", m); else buf[0] = 0; }

/* ---- MS multibyte (SBCS -- __MB_CAPABLE is off, so a byte is a character) ---- */
size_t _mbstrlen(const char *s)                                    { return strlen(s); }
unsigned char *_mbsnbcpy(unsigned char *dst, const unsigned char *src, size_t n) {
    size_t i = 0; for (; i < n && src[i]; ++i) dst[i] = src[i]; if (i < n) dst[i] = 0; return dst;
}
unsigned char *_mbsnbcat(unsigned char *dst, const unsigned char *src, size_t n) {
    size_t len = strlen((char *)dst); size_t i = 0;
    for (; i < n && src[i]; ++i) dst[len + i] = src[i]; dst[len + i] = 0; return dst;
}

/* ---- FILE helpers ---- */
FILE *_fsopen(const char *name, const char *mode, int shflag) { (void)shflag; return fopen(name, mode); }
static FILE *rxdk_wfopen(const wchar_t *path, const wchar_t *mode) {
    char p[512], m[16];
    if (wcstombs(p, path, sizeof p) == (size_t)-1) return NULL;
    if (wcstombs(m, mode, sizeof m) == (size_t)-1) return NULL;
    return fopen(p, m);
}
FILE *_wfopen(const wchar_t *path, const wchar_t *mode)              { return rxdk_wfopen(path, mode); }
FILE *_wfsopen(const wchar_t *path, const wchar_t *mode, int sh)    { (void)sh; return rxdk_wfopen(path, mode); }
int _wfopen_s(FILE **pf, const wchar_t *path, const wchar_t *mode)  { if (!pf) return EINVAL; *pf = rxdk_wfopen(path, mode); return *pf ? 0 : errno; }
int _fseeki64(FILE *f, long long off, int origin)                   { return fseek(f, (long)off, origin); }  /* files are < 2GB */
void _lock_file(FILE *f)   { (void)f; }   /* stdio locking: single flow here */
void _unlock_file(FILE *f) { (void)f; }

/* ---- aligned allocation (matched pair; base pointer stashed below block) ---- */
void *_aligned_malloc(size_t size, size_t align) {
    if (align < sizeof(void *)) align = sizeof(void *);
    void *raw = malloc(size + align + sizeof(void *));
    if (!raw) return NULL;
    uintptr_t a = ((uintptr_t)raw + sizeof(void *) + (align - 1)) & ~(uintptr_t)(align - 1);
    ((void **)a)[-1] = raw;
    return (void *)a;
}
void _aligned_free(void *p) { if (p) free(((void **)p)[-1]); }

/* ---- context-passing qsort (single flow: stash the comparator) ---- */
static int (*g_qs_cmp)(void *, const void *, const void *);
static void *g_qs_ctx;
static int rxdk_qs_tramp(const void *a, const void *b) { return g_qs_cmp(g_qs_ctx, a, b); }
void qsort_s(void *base, size_t num, size_t size,
             int (*cmp)(void *, const void *, const void *), void *ctx) {
    g_qs_cmp = cmp; g_qs_ctx = ctx;
    qsort(base, num, size, rxdk_qs_tramp);
}

/* ---- misc ---- */
/* _HUGE: MS CRT's HUGE_VAL data global (used by math error paths / <float.h>). */
double _HUGE = __builtin_huge_val();
/* MS _tolower/_toupper: the case-mappers (map to the standard ones -- close
   enough; MS's are the unchecked fast forms). */
int _tolower(int c) { return tolower(c); }
int _toupper(int c) { return toupper(c); }
char *_gcvt(double v, int ndig, char *buf) { snprintf(buf, (size_t)ndig + 8, "%.*g", ndig, v); return buf; }
_Noreturn void _invoke_watson(const wchar_t *e, const wchar_t *f, const wchar_t *fi, unsigned l, uintptr_t r) {
    (void)e; (void)f; (void)fi; (void)l; (void)r; abort();
}
/* ---- _beginthreadex over the kernel thread primitive ----
   MS: uintptr_t _beginthreadex(void *security, unsigned stack,
                                unsigned (*start)(void *), void *arg,
                                unsigned initflag, unsigned *thrdaddr)
   returns a thread HANDLE usable with the Wait/Close kernel calls. ExCreateThread
   runs start(arg) directly and the thread ends when start returns (so the unused
   _endthreadex is unnecessary). PPC has a single calling convention, so the MS
   unsigned(*)(void*) start is ABI-compatible with the void* entry ExCreateThread
   expects. */
extern unsigned ExCreateThread(unsigned *handle, unsigned stack_size, unsigned *tid,
                               unsigned xapi_startup, void *start, void *ctx,
                               unsigned flags);
uintptr_t _beginthreadex(void *security, unsigned stack,
                         unsigned (*start)(void *), void *arg,
                         unsigned initflag, unsigned *thrdaddr) {
    (void)security;
    unsigned handle = 0, tid = 0;
    if (ExCreateThread(&handle, stack ? stack : 0x40000u, &tid, 0,
                       (void *)start, arg, initflag) != 0)
        return 0;
    if (thrdaddr) *thrdaddr = tid;
    return (uintptr_t)handle;
}

/* ---- _wsplitpath: decompose a path into drive/dir/fname/ext (any out NULL) ---- */
void _wsplitpath(const wchar_t *path, wchar_t *drive, wchar_t *dir,
                 wchar_t *fname, wchar_t *ext) {
    const wchar_t *p = path, *slash = NULL, *dot = NULL;
    if (drive) drive[0] = 0;
    if (dir)   dir[0] = 0;
    if (fname) fname[0] = 0;
    if (ext)   ext[0] = 0;
    if (!path) return;
    if (path[0] && path[1] == L':') {                 /* "X:" drive */
        if (drive) { drive[0] = path[0]; drive[1] = L':'; drive[2] = 0; }
        p = path + 2;
    }
    for (const wchar_t *q = p; *q; ++q) {
        if (*q == L'\\' || *q == L'/') slash = q;
        else if (*q == L'.') dot = q;
    }
    const wchar_t *name = slash ? slash + 1 : p;      /* first char of filename */
    if (dot && dot < name) dot = NULL;                /* a '.' in the dir is not an ext */
    if (dir) { size_t n = (size_t)(name - p); wmemcpy(dir, p, n); dir[n] = 0; }
    const wchar_t *nameend = dot ? dot : name + wcslen(name);
    if (fname) { size_t n = (size_t)(nameend - name); wmemcpy(fname, name, n); fname[n] = 0; }
    if (ext && dot) wcscpy(ext, dot);
}

/* ---- __iob_func: MS's stdin/stdout/stderr accessor ----
   MS code takes stdin/stdout/stderr as &__iob_func()[0..2], indexing with MS's
   own sizeof(FILE) == sizeof(struct _iobuf) == 32 bytes. We return an array of 3
   slots padded to that 32-byte stride, each starting with a real (unbuffered)
   picolibc `struct __file` whose put/get/flush DELEGATE to our actual
   stdin/stdout/stderr -- so a shipped lib's fprintf(stderr, ...) reaches the same
   console sink, sharing our line buffering (no one-char-per-line debug spam).
   struct __file (~20B: unget + flags + 3 fn ptrs) fits the 32-byte slot. */
#define RXDK_MS_FILE_SIZE 32
_Static_assert(sizeof(struct __file) <= RXDK_MS_FILE_SIZE, "picolibc FILE exceeds MS _iobuf stride");

extern FILE *const __posix_stdin;
extern FILE *const __posix_stdout;
extern FILE *const __posix_stderr;

static int iob_put_out(char c, struct __file *f) { (void)f; return putc((unsigned char)c, __posix_stdout); }
static int iob_put_err(char c, struct __file *f) { (void)f; return putc((unsigned char)c, __posix_stderr); }
static int iob_get_in(struct __file *f)          { (void)f; return getc(__posix_stdin); }
static int iob_flush_out(struct __file *f)       { (void)f; return fflush(__posix_stdout); }
static int iob_flush_err(struct __file *f)       { (void)f; return fflush(__posix_stderr); }

typedef union { struct __file f; char pad[RXDK_MS_FILE_SIZE]; } rxdk_ms_iob;
static rxdk_ms_iob g_ms_iob[3];

FILE *__iob_func(void) {
    static int inited;
    if (!inited) {
        struct __file in  = { 0 }; in.flags  = __SRD; in.get  = iob_get_in;
        struct __file out = { 0 }; out.flags = __SWR; out.put = iob_put_out; out.flush = iob_flush_out;
        struct __file err = { 0 }; err.flags = __SWR; err.put = iob_put_err; err.flush = iob_flush_err;
        g_ms_iob[0].f = in; g_ms_iob[1].f = out; g_ms_iob[2].f = err;
        inited = 1;
    }
    return (FILE *)&g_ms_iob[0].f;
}

/* _isctype(c, mask): test an int against the MS <ctype.h> classification bits. */
#define _MS_UPPER 0x1
#define _MS_LOWER 0x2
#define _MS_DIGIT 0x4
#define _MS_SPACE 0x8
#define _MS_PUNCT 0x10
#define _MS_CONTROL 0x20
#define _MS_BLANK 0x40
#define _MS_HEX 0x80
int _isctype(int c, int mask) {
    int r = 0;
    if (isupper(c))  r |= _MS_UPPER;
    if (islower(c))  r |= _MS_LOWER;
    if (isdigit(c))  r |= _MS_DIGIT;
    if (isspace(c))  r |= _MS_SPACE;
    if (ispunct(c))  r |= _MS_PUNCT;
    if (iscntrl(c))  r |= _MS_CONTROL;
    if (c == ' ' || c == '\t') r |= _MS_BLANK;
    if (isxdigit(c)) r |= _MS_HEX;
    return r & mask;
}

/* ============================================================================
 * MS CRT locale / ctype / time internals imported by cl.exe C++ libs (notably
 * vcomp.lib's bundled MS-STL). Our runtime is single-locale ("C"); these give
 * the MS-spelled entry points over that. See the vcomp import audit.
 * ==========================================================================*/

/* __pctype_func(): the MS <ctype.h> classification table, indexed [(uchar)c].
   Same bit layout as _isctype above, plus _ALPHA (0x0100). */
#define _MS_ALPHA 0x0100
static unsigned short g_ms_pctype[256];
const unsigned short *__pctype_func(void) {
    static int inited;
    if (!inited) {
        for (int c = 0; c < 256; ++c) {
            unsigned short m = 0;
            if (isupper(c)) m |= _MS_UPPER | _MS_ALPHA;
            if (islower(c)) m |= _MS_LOWER | _MS_ALPHA;
            if (isdigit(c)) m |= _MS_DIGIT;
            if (isspace(c)) m |= _MS_SPACE;
            if (ispunct(c)) m |= _MS_PUNCT;
            if (iscntrl(c)) m |= _MS_CONTROL;
            if (c == ' ' || c == '\t') m |= _MS_BLANK;
            if (isxdigit(c)) m |= _MS_HEX;
            g_ms_pctype[c] = m;
        }
        inited = 1;
    }
    return g_ms_pctype;
}

/* Locale-data accessors. For the "C" locale these are the neutral defaults the
   MS-STL expects: 1-byte mb, no codepage/handle (LCID 0). */
int      ___mb_cur_max_func(void)   { return 1; }
unsigned ___lc_codepage_func(void)  { return 0; }
unsigned ___lc_collate_cp_func(void){ return 0; }
void    *___lc_handle_func(void)    { static long h[6]; return h; } /* 6 LC_* categories, all 0 */

/* Time-locale name lists, MS colon-delimited format (abbrev then full, leading
   ':'); consumed by the MS strftime path for %a/%A and %b/%B. "C" locale. */
char *_Getdays(void) {
    static char s[] = ":Sun:Sunday:Mon:Monday:Tue:Tuesday:Wed:Wednesday"
                      ":Thu:Thursday:Fri:Friday:Sat:Saturday";
    return s;
}
char *_Getmonths(void) {
    static char s[] = ":Jan:January:Feb:February:Mar:March:Apr:April:May:May"
                      ":Jun:June:Jul:July:Aug:August:Sep:September:Oct:October"
                      ":Nov:November:Dec:December";
    return s;
}

/* _Gettnames(): MS __lc_time_data blob. The MS-STL <locale> time facets read it,
   but the OpenMP runtime never formats times, so a zeroed "C"-locale block only
   has to satisfy the link. */
void *_Gettnames(void) { static char tnames[512]; return tnames; } /* RXDK-STUB */

/* _Strftime(): MS internal strftime worker (last arg is __lc_time_data, which we
   don't consult -- our strftime is already "C" locale). */
size_t _Strftime(char *s, size_t max, const char *fmt, const struct tm *t,
                 void *lc_time) {
    (void)lc_time;
    return strftime(s, max, fmt, t);
}

/* rand_s(): MS secure RNG. Not cryptographic here -- an xorshift64* seeded from
   address/counter entropy; adequate for the STL uses (seeding, shuffles). */
int rand_s(unsigned int *p) {
    if (!p) { errno = EINVAL; return EINVAL; }
    static unsigned long long s;
    if (!s) s = 0x9E3779B97F4A7C15ULL ^ (unsigned long long)(uintptr_t)&p;
    s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
    *p = (unsigned)((s * 0x2545F4914F6CDD1DULL) >> 32);
    return 0;
}

/* ---- MS debug-CRT heap shims (imported by the *d debug libs, e.g. vcompd) ----
   The _*_dbg heap wrappers forward to our real allocator (we track no debug
   block headers); _CrtDbgReportW is a no-op; _chvalidator is the debug ctype
   range validator (identity here). */
void *_malloc_dbg(size_t size, int blockType, const char *file, int line) {
    (void)blockType; (void)file; (void)line; return malloc(size);
}
void *_calloc_dbg(size_t num, size_t size, int blockType, const char *file, int line) {
    (void)blockType; (void)file; (void)line; return calloc(num, size);
}
void *_realloc_dbg(void *p, size_t size, int blockType, const char *file, int line) {
    (void)blockType; (void)file; (void)line; return realloc(p, size);
}
void _free_dbg(void *p, int blockType) { (void)blockType; free(p); }
int _CrtDbgReportW(int reportType, const wchar_t *file, int line,
                   const wchar_t *module, const wchar_t *fmt, ...) {
    (void)reportType; (void)file; (void)line; (void)module; (void)fmt; return 0;
}
int _chvalidator(int c) { return c; }

/* __IsIntConst is an Xbox 360 compiler intrinsic (ppcintrinsics.h:
   `int __IsIntConst(int)`) that reports whether its argument was a
   compile-time constant; d3d9.h's D3DISINTCONST fast-paths use it to pick an
   optimised state-set path. The MS compiler folds it at compile time; clang
   does not, so it emits a real call. A runtime call means "not a constant", so
   return 0 -- the caller then takes its general (non-constant) path. */
int __IsIntConst(int val) { (void)val; return 0; }

/* ---- version-stamp + XAPI globals referenced by vcomp / the MS CRT ----
   Data symbols the shipped libs expect the CRT/XAPI to define. XapiProcessHeap
   is the process heap handle GetProcessHeap() returns; it is initialised to a
   real kernel heap by XAPI startup (crt_start). For link-completeness here it is
   a definition; runtime heap wiring is in the startup path. */
/* weak: a self-contained lib (e.g. vcomp) may bundle its own strong copies --
   ours are the fallback so both link orders resolve without collision. */
__attribute__((weak)) unsigned VCOMPBuildNumber = 0;
__attribute__((weak)) unsigned __CrtBuildNumber = 0;
__attribute__((weak)) void *XapiProcessHeap = 0;          /* startup sets a real heap */
__attribute__((weak)) void *XapiCurrentTopLevelFilter = 0;
/* NtGlobalFlag is an xboxkrnl data export the heap manager (RtlCreateHeap/
   RtlAllocateHeap) reads to enable debug-heap behaviour (tail checking, free
   fill, stack traces). The public XDK import libraries do not export it, so a
   title cannot import it; a normal retail process runs with it clear. Provide it
   as zero -- the ordinary, non-instrumented value -- so the heap manager takes
   its normal path on both console and xenia. */
__attribute__((weak)) unsigned NtGlobalFlag = 0;
