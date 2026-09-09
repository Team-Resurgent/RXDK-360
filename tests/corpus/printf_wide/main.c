#include "rt.h"
void title_main(void) {
    char b[128];
    sprintf(b, "S=[%S]", L"WideStr");
    DbgPrint("%s\n", b);
    sprintf(b, "ls=[%ls]", L"WideLS");
    DbgPrint("%s\n", b);
}
