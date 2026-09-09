/* <stdckdint.h> -- C23 checked integer arithmetic. ckd_add/sub/mul compute in
   infinite precision, store the wrapped result, and return true on overflow. */
#include "rxdk_test.h"
#include <stdckdint.h>
#include <limits.h>

int main(void) {
    int r = 0;
    CHECK(!ckd_add(&r, 2, 3) && r == 5, "ckd_add no overflow (2+3=5)");
    CHECK(ckd_add(&r, INT_MAX, 1), "ckd_add detects INT_MAX+1 overflow");
    CHECK(!ckd_sub(&r, 5, 8) && r == -3, "ckd_sub no overflow (5-8=-3)");
    CHECK(ckd_sub(&r, INT_MIN, 1), "ckd_sub detects INT_MIN-1 overflow");
    CHECK(!ckd_mul(&r, 1000, 1000) && r == 1000000, "ckd_mul no overflow");
    CHECK(ckd_mul(&r, INT_MAX, 2), "ckd_mul detects overflow");

    /* mixed/narrow types: the check is against the destination type. */
    unsigned char uc = 0;
    CHECK(!ckd_add(&uc, (unsigned char)100, (unsigned char)100) && uc == 200,
          "ckd_add u8 100+100=200 fits");
    CHECK(ckd_add(&uc, (unsigned char)200, (unsigned char)100),
          "ckd_add u8 200+100 overflows a byte");

    unsigned u = 0;
    CHECK(ckd_sub(&u, 0u, 1u), "ckd_sub unsigned 0-1 underflows");

    CHECK_DONE("ckdint");
    return 0;
}
