#include "rt.h"

/* Regression for issue #1: clang compiling for PowerPC emits calls to small
   out-of-line compiler-helper routines -- the MS register save/restore family
   (__savegprlr_N and __restgprlr_N, __savefpr_N, __savevmx_N), the
   64-bit-int-to-double conversion __u64tod, and the stack probe _RtlCheckStack.
   The modern runtime (build/libc/libc.a) bundles these from the translated
   libcMT; if they are ever dropped, a non-trivial clang title fails to LINK
   against the modern runtime -- so this test simply BUILDING and RUNNING in the
   default (modern) corpus link is the regression. The symbol-level guard lives
   in tools/test_compiler_helpers.py; this exercises the code paths at runtime.

   __u64tod and the large-frame stack probe are forced here directly; the
   register-save helpers are pulled by crunch()'s many long-lived locals held
   across an external call. */

static unsigned long long __attribute__((noinline))
crunch(unsigned long long a, unsigned long long b) {
    /* ten values kept live across the DbgPrint call: the compiler must preserve
       them in callee-saved registers, whose prologue/epilogue save+restore is
       the __savegprlr_N and __restgprlr_N helper path. */
    volatile unsigned long long r0 = a, r1 = b, r2 = a ^ b, r3 = a + b, r4 = a - b;
    volatile unsigned long long r5 = a * 3, r6 = b * 5, r7 = a | b, r8 = a & b, r9 = a ^ (b << 1);
    DbgPrint("");
    return r0 + r1 + r2 + r3 + r4 + r5 + r6 + r7 + r8 + r9;
}

void t_regsave(void) {
    volatile unsigned char frame[0x4000];    /* large frame -> _RtlCheckStack */
    frame[0] = 1;
    frame[0x3FFF] = 2;
    unsigned long long s = crunch(0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL);
    double d = (double)s;                    /* uint64 -> double -> __u64tod */
    DbgPrint("regsave lo=%u hi=%u frame=%d dpos=%d\n",
             (unsigned)s, (unsigned)(s >> 32),
             (int)(frame[0] + frame[0x3FFF]), d > 0.0);
}
