#include "rt.h"
void title_main(void) {
    int* p = (int*)malloc(4 * sizeof(int));
    for (int i = 0; i < 4; i++) p[i] = (i + 1) * 11;
    DbgPrint("heap=%d,%d,%d,%d\n", p[0], p[1], p[2], p[3]);
    free(p);
    DbgPrint("freed\n");
}
