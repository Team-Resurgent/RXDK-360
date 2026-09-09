#include "rt.h"

/* Regression test for the MS-PPC frame-lowering LR-slot bug: printf() to the
   buffered console FILE with several % conversions, then getchar() from the
   buffered stdin FILE. This is the exact path (varargs printf over the bufio
   stream + getchar) that a frame bug corrupted -- printf saved LR at CallerSP-8
   and the frame layout put a va_list local on the same slot, so printf returned
   to a stack address. printf_basic only uses sprintf and never caught it.
   Declared locally; rt.h intentionally only exposes DbgPrint/sprintf. */
extern int printf(const char *, ...);
extern int getchar(void);
typedef long     rxdk_ssize;
typedef unsigned rxdk_usize;
extern void rxdk_set_stdin_handler(rxdk_ssize (*)(void *, rxdk_usize));

static rxdk_ssize feed(void *buf, rxdk_usize n) {
    static const char line[] = "Zx";
    static rxdk_usize pos;
    rxdk_usize i = 0;
    char *out = (char *)buf;
    while (i < n && line[pos]) out[i++] = line[pos++];
    return (rxdk_ssize)i;
}

void t_printf_stream(void) {
    printf("d=%d u=%u x=%x s=%s c=%c pct=%%\n", -5, 5u, 255, "hi", 'Z');
    rxdk_set_stdin_handler(feed);
    int c = getchar();
    rxdk_set_stdin_handler(0);
    DbgPrint("getchar=%d\n", c);
}
