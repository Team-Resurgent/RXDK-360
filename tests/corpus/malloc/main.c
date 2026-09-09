#include "rt.h"
void t_malloc(void) {
    int* p = (int*)malloc(4 * sizeof(int));
    for (int i = 0; i < 4; i++) p[i] = (i + 1) * 11;
    DbgPrint("heap=%d,%d,%d,%d\n", p[0], p[1], p[2], p[3]);
    free(p);
    DbgPrint("freed\n");
    /* the allocator must 16-align (VMX128 / __vector4 requirement) */
    void* a = malloc(1);
    void* b = malloc(64);
    DbgPrint("align16=%d\n", (((unsigned long)a | (unsigned long)b) & 15u) == 0);
    free(a); free(b);
}
