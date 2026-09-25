#include "rt.h"
/* scanf/swscanf value-type coverage -- the corpus had NO scanf coverage at all.
   The wide float path (swscanf L"%f %f %f") is exactly what ATG's XATG scene
   parser (AtgSceneFileParser swscanf_s) drives, and where AdvancedLighting
   faults inside picolibc vfwscanf. swscanf_s forwards to the same vswscanf, so
   swscanf reproduces it. Ordered narrow-first so a wide-path wedge is visible. */
extern "C" void t_scanf_types(void) {
    char b[160];

    /* narrow integer types */
    int d = 0; unsigned u = 0, x = 0;
    int n = sscanf("-42 7 ff", "%d %u %x", &d, &u, &x);
    DbgPrint("narrow int n=%d d=%d u=%u x=%x\n", n, d, u, x);

    /* narrow float */
    float f1 = 0, f2 = 0, f3 = 0;
    n = sscanf("1.5 2.25 3.75", "%f %f %f", &f1, &f2, &f3);
    sprintf(b, "narrow flt n=%d a=%f b=%f c=%f", n, f1, f2, f3);
    DbgPrint("%s\n", b);

    /* wide integer */
    int wd = 0;
    n = swscanf(L"123", L"%d", &wd);
    DbgPrint("wide int n=%d d=%d\n", n, wd);

    /* wide float -- the AtgSceneFileParser path */
    float w1 = 0, w2 = 0, w3 = 0;
    n = swscanf(L"1.5 2 3.5", L"%f %f %f", &w1, &w2, &w3);
    sprintf(b, "wide flt n=%d a=%f b=%f c=%f", n, w1, w2, w3);
    DbgPrint("%s\n", b);
}
