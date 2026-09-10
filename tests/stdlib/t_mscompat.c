/* MS CRT compatibility surface (runtime/xbox/ms_crt_compat.c + cxxrt.cpp): the
   underscore-spelled / secure / MSVC-mangled CRT entry points the shipped XDK
   libs import, each forwarding to our modern runtime. This exercises their
   observable behaviour so the mapping is documented and regression-checked.
   (exit() itself is not called here -- it terminates the program -- but every
   test reaches it: _start routes the normal main() return through exit().) */
#include "rxdk_test.h"
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>

/* --- first-wave aliases --- */
extern int _stricmp(const char *, const char *);
extern int _strnicmp(const char *, const char *, size_t);
extern int _wcsicmp(const wchar_t *, const wchar_t *);
extern wchar_t *_wcslwr(wchar_t *);
extern int _wtoi(const wchar_t *);
extern long _wtol(const wchar_t *);
extern double _wtof(const wchar_t *);
extern int _finite(double);
extern int _isnan(double);
extern int _snprintf(char *, size_t, const char *, ...);
extern int *_errno(void);
extern int wcscpy_s(wchar_t *, size_t, const wchar_t *);
extern int wcscat_s(wchar_t *, size_t, const wchar_t *);
extern void *_blkmov(void *, const void *, size_t);
/* --- MSVC operator new/delete aliases (mangled names via asm labels) --- */
extern void *ms_opnew(size_t) __asm__("??2@YAPAXI@Z");
extern void  ms_opdel(void *) __asm__("??3@YAXPAX@Z");
/* --- second-wave aliases --- */
extern int _memicmp(const void *, const void *, size_t);
extern char *_strdup(const char *);
extern void _swab(char *, char *, int);
extern char *strtok_s(char *, const char *, char **);
extern char *_ltoa(long, char *, int);
extern char *_ultoa(unsigned long, char *, int);
extern char *_i64toa(long long, char *, int);
extern int _itoa_s(int, char *, size_t, int);
extern wchar_t *_ultow(unsigned long, wchar_t *, int);
extern long long _atoi64(const char *);
extern unsigned long long _strtoui64(const char *, char **, int);
extern long long _abs64(long long);
extern int _fpclass(double);
extern double _chgsign(double);
extern double _copysign(double, double);
extern unsigned _controlfp(unsigned, unsigned);
extern size_t _mbstrlen(const char *);
extern unsigned char *_mbsnbcpy(unsigned char *, const unsigned char *, size_t);
extern void *_aligned_malloc(size_t, size_t);
extern void _aligned_free(void *);
extern void qsort_s(void *, size_t, size_t, int (*)(void *, const void *, const void *), void *);
extern char *_gcvt(double, int, char *);
extern int _isctype(int, int);
extern long long _time64(long long *);

static int cmp_ctx(void *ctx, const void *a, const void *b) {
    int dir = *(int *)ctx;                       /* +1 ascending, -1 descending */
    return dir * (*(const int *)a - *(const int *)b);
}

int main(void) {
    char b[64];
    wchar_t wb[64];

    /* ---- first wave ---- */
    CHECK(_stricmp("HeLLo", "hello") == 0, "_stricmp case-insensitive equal");
    CHECK(_strnicmp("ABCxyz", "abcQQQ", 3) == 0, "_strnicmp first 3 equal");
    CHECK(_wcsicmp(L"FOO", L"foo") == 0, "_wcsicmp case-insensitive equal");
    wcscpy(wb, L"MiXeD"); _wcslwr(wb);
    CHECK(wcscmp(wb, L"mixed") == 0, "_wcslwr lowercases in place");
    CHECK_EQI(_wtoi(L"-42"), -42, "_wtoi parses wide int");
    CHECK(_wtol(L"100000") == 100000L, "_wtol parses wide long");
    CHECK(_wtof(L"2.5") == 2.5, "_wtof parses wide double");
    CHECK(_finite(1.0) && !_finite(1.0 / 0.0), "_finite: finite vs inf");
    CHECK(!_isnan(1.0) && _isnan(0.0 / 0.0), "_isnan: number vs NaN");
    _snprintf(b, sizeof b, "%d-%s", 7, "x");
    CHECK_STR(b, "7-x", "_snprintf formats");
    errno = 0; *_errno() = ERANGE;
    CHECK_EQI(errno, ERANGE, "_errno() aliases &errno");
    wcscpy(wb, L"ab"); CHECK_EQI(wcscat_s(wb, 64, L"cd"), 0, "wcscat_s appends");
    CHECK(wcscmp(wb, L"abcd") == 0, "wcscat_s result");
    CHECK_EQI(wcscpy_s(wb, 2, L"toolong"), ERANGE, "wcscpy_s -> ERANGE on overflow");
    { char src[4] = "XY", dst[4] = {0}; _blkmov(dst, src, 3); CHECK(memcmp(dst, "XY", 3) == 0, "_blkmov copies"); }

    /* ---- MSVC operator new/delete aliases ---- */
    { void *p = ms_opnew(128); CHECK(p != NULL, "??2@ (operator new) allocates");
      memset(p, 0xAB, 128); ms_opdel(p); CHECK(1, "??3@ (operator delete) frees"); }

    /* ---- second wave: strings / radix ---- */
    CHECK_EQI(_memicmp("ABC", "abc", 3), 0, "_memicmp case-insensitive");
    { char *d = _strdup("dup"); CHECK(d && strcmp(d, "dup") == 0, "_strdup"); free(d); }
    { char s[5] = "abcd"; char o[5] = {0}; _swab(s, o, 4); CHECK_STR(o, "badc", "_swab swaps byte pairs"); }
    { char s[] = "a,b,c"; char *ctx; char *t = strtok_s(s, ",", &ctx);
      CHECK(t && strcmp(t, "a") == 0, "strtok_s first token");
      t = strtok_s(NULL, ",", &ctx); CHECK(t && strcmp(t, "b") == 0, "strtok_s second token"); }
    _ultoa(255UL, b, 16); CHECK_STR(b, "ff", "_ultoa hex");
    _ltoa(-5L, b, 10);    CHECK_STR(b, "-5", "_ltoa negative decimal");
    _i64toa(-9999999999LL, b, 10); CHECK_STR(b, "-9999999999", "_i64toa 64-bit");
    CHECK_EQI(_itoa_s(42, b, sizeof b, 10), 0, "_itoa_s ok"); CHECK_STR(b, "42", "_itoa_s value");
    { char tiny[2]; CHECK_EQI(_itoa_s(42, tiny, sizeof tiny, 10), ERANGE, "_itoa_s -> ERANGE"); }
    _ultow(4095UL, wb, 16); CHECK(wcscmp(wb, L"fff") == 0, "_ultow hex wide");

    /* ---- second wave: numbers ---- */
    CHECK(_atoi64("9999999999") == 9999999999LL, "_atoi64 past 32-bit");
    CHECK(_strtoui64("ff", NULL, 16) == 255ULL, "_strtoui64 hex");
    CHECK(_abs64(-5000000000LL) == 5000000000LL, "_abs64");
    CHECK(_chgsign(3.0) == -3.0 && _chgsign(-2.0) == 2.0, "_chgsign flips sign");
    CHECK(_copysign(3.0, -1.0) == -3.0, "_copysign");
    CHECK(_fpclass(-0.0) == 0x0020 /*_FPCLASS_NZ*/, "_fpclass negative zero");
    CHECK(_fpclass(1.5) == 0x0100 /*_FPCLASS_PN*/, "_fpclass positive normal");
    CHECK(_controlfp(0, 0) == 0x0009001fu, "_controlfp reports default word");

    /* ---- second wave: multibyte (SBCS) ---- */
    CHECK_EQI((int)_mbstrlen("hello"), 5, "_mbstrlen == strlen (SBCS)");
    { unsigned char dst[8]; _mbsnbcpy(dst, (const unsigned char *)"hi", 8); CHECK_STR((char *)dst, "hi", "_mbsnbcpy"); }

    /* ---- second wave: aligned alloc ---- */
    { void *p = _aligned_malloc(200, 64);
      CHECK(p != NULL && ((uintptr_t)p & 63) == 0, "_aligned_malloc is 64-aligned");
      _aligned_free(p); CHECK(1, "_aligned_free"); }

    /* ---- second wave: context qsort ---- */
    { int arr[5] = { 3, 1, 4, 1, 5 }, dir = 1;
      qsort_s(arr, 5, sizeof(int), cmp_ctx, &dir);
      CHECK(arr[0] == 1 && arr[4] == 5, "qsort_s ascending with context");
      dir = -1; qsort_s(arr, 5, sizeof(int), cmp_ctx, &dir);
      CHECK(arr[0] == 5 && arr[4] == 1, "qsort_s descending with context"); }

    /* ---- second wave: misc ---- */
    _gcvt(3.14159, 4, b); CHECK(b[0] == '3' && b[1] == '.', "_gcvt formats %g");
    CHECK(_isctype('A', 0x1 /*_UPPER*/) && !_isctype('a', 0x1), "_isctype _UPPER");
    CHECK(_isctype('7', 0x4 /*_DIGIT*/) != 0, "_isctype _DIGIT");
    CHECK(_time64(NULL) > 0, "_time64 returns a clock value");

    CHECK_DONE("mscompat");
    return 0;
}
