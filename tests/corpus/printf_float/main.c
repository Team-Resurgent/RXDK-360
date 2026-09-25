#include "rt.h"
/* Floating-point printf conversions -- previously untested (the corpus covered
   %d/%u/%x/%o/%s/%c and wide %S/%ls, but never %f/%e/%g). */
void t_printf_float(void) {
    char b[128];
    sprintf(b, "f=%f", 3.140000);
    DbgPrint("%s\n", b);
    sprintf(b, "e=%e g=%g", 12345.0, 0.5);
    DbgPrint("%s\n", b);
    sprintf(b, "p=%.2f w=%8.3f neg=%f", 2.5, 1.5, -0.25);
    DbgPrint("%s\n", b);
}
