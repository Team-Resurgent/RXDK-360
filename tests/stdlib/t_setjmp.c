/* setjmp/longjmp non-local jump (C). */
#include "rxdk_test.h"
#include <setjmp.h>

static jmp_buf jb;

static void deep(int depth) {
    if (depth == 0) longjmp(jb, 99);
    volatile int pad[8];
    for (int i = 0; i < 8; ++i) pad[i] = depth;
    deep(depth - 1);
    (void)pad;
}

int main(void) {
    int r = setjmp(jb);
    if (r == 0) {
        CHECK(1, "setjmp initial return is 0");
        deep(10);
        CHECK(0, "unreachable after longjmp");
    } else {
        CHECK_EQI(r, 99, "longjmp delivers the value");
    }

    /* second use of the same buffer */
    volatile int stage = 0;
    if (setjmp(jb) == 0) {
        stage = 1;
        longjmp(jb, 7);
    } else {
        CHECK_EQI(stage, 1, "state before longjmp is preserved");
    }

    CHECK_DONE("setjmp");
    return 0;
}
