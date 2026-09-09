#include "rt.h"
void t_printf_basic(void) {
    char b[160];
    sprintf(b, "d=%d u=%u x=%x X=%08X o=%o", -5, 5u, 255, 255, 8);
    DbgPrint("%s\n", b);
    sprintf(b, "s=[%s] c=%c pct=%%", "hi", 'Z');
    DbgPrint("%s\n", b);
    sprintf(b, "w=[%5d] l=[%-5d] z=[%05d] neg=[%+d]", 42, 42, 7, 9);
    DbgPrint("%s\n", b);
}
