#include "rt.h"
void title_main(void) {
    char buf[32], buf2[32];
    strcpy(buf, "abcdef");
    DbgPrint("len=%d\n", (int)strlen(buf));
    memcpy(buf2, buf, 7);
    DbgPrint("cmp=%d\n", memcmp(buf, buf2, 7));
    DbgPrint("copy=%s\n", buf2);
}
