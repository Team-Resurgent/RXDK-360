#include "rt.h"
void t_arith(void) {
    int a = 6, b = 7;
    DbgPrint("mul=%d\n", a * b);
    DbgPrint("sum=%d\n", 1 + 2 + 3 + 4);
    DbgPrint("neg=%d\n", 3 - 10);
}
