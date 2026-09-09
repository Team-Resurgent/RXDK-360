#include "rt.h"
void t_printf_wide(void) {
    char b[128];
    sprintf(b, "S=[%S]", L"WideStr");
    DbgPrint("%s\n", b);
    sprintf(b, "ls=[%ls]", L"WideLS");
    DbgPrint("%s\n", b);
}
