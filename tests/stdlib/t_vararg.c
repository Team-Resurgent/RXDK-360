/* va_arg overflow-area coverage. Arguments past the ~6 that fit in the PowerPC
   argument GPRs spill to the va_list overflow area. The Xbox 360 ABI passes
   every argument in an 8-byte, big-endian right-justified slot; a bug in the
   32-bit-base LowerVAARG read the overflow slots at the wrong offset and stride
   (4 bytes instead of 8, from the padding word), so every argument past the
   registers came back 0 or garbage. That faulted ATG's 16-float matrix
   swscanf_s on a real kit. This exercises the same >8-argument path with no
   libc, so a regressed va_arg lowering fails here directly. */
#include <stdarg.h>
#include "rxdk_test.h"

static long sum_ints(int n, ...) {
    va_list ap;
    long s = 0;
    va_start(ap, n);
    for (int i = 0; i < n; i++)
        s += va_arg(ap, int);
    va_end(ap);
    return s;
}

/* Two named params (like scanf's s,fmt) push the varargs so ~6 land in GPRs and
   the rest spill -- the exact shape that broke. */
static void grab_ints(int n, int *out, ...) {
    va_list ap;
    va_start(ap, out);
    for (int i = 0; i < n; i++)
        out[i] = va_arg(ap, int);
    va_end(ap);
}

static void grab_ptrs(int n, void **out, ...) {
    va_list ap;
    va_start(ap, out);
    for (int i = 0; i < n; i++)
        out[i] = va_arg(ap, void *);
    va_end(ap);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    CHECK_EQI(sum_ints(4, 1, 2, 3, 4), 10, "4 int varargs (registers only)");
    CHECK_EQI(sum_ints(16, 1, 2, 3, 4, 5, 6, 7, 8,
                           9, 10, 11, 12, 13, 14, 15, 16),
              136, "16 int varargs (spills to overflow area)");

    int v[16];
    for (int i = 0; i < 16; i++) v[i] = -1;
    grab_ints(16, v, 10, 11, 12, 13, 14, 15, 16, 17,
                     18, 19, 20, 21, 22, 23, 24, 25);
    CHECK_EQI(v[0], 10, "int vararg 1 (register)");
    CHECK_EQI(v[5], 15, "int vararg 6 (last register)");
    CHECK_EQI(v[6], 16, "int vararg 7 (first overflow slot)");
    CHECK_EQI(v[8], 18, "int vararg 9 (overflow)");
    CHECK_EQI(v[15], 25, "int vararg 16 (last overflow)");

    /* Pointer varargs use the same integer-slot path -- this is exactly what a
       scanf destination-pointer list is. */
    int a, b, c, d, e, f, g, h, i9, j;
    void *pv[10];
    for (int k = 0; k < 10; k++) pv[k] = 0;
    grab_ptrs(10, pv, &a, &b, &c, &d, &e, &f, &g, &h, &i9, &j);
    CHECK(pv[0] == &a, "ptr vararg 1 (register)");
    CHECK(pv[7] == &h, "ptr vararg 8 (overflow)");
    CHECK(pv[8] == &i9, "ptr vararg 9 (overflow)");
    CHECK(pv[9] == &j, "ptr vararg 10 (overflow)");

    CHECK_DONE("vararg");
    return 0;
}
