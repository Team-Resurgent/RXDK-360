/* Raw C <string.h> mem/str primitives. The generic implementations (memchr,
   memcmp, memcpy, memmove, memset, strchr, strlen) were dropped by the picolibc
   consolidation and recovered for the 360. memcpy/memmove/memchr/strchr in
   particular had no explicit test in this suite -- only implicit linkage -- so a
   future regression in one of them could pass unnoticed. Reference them directly. */
#include "rxdk_test.h"
#include <string.h>

int main(void) {
    char buf[32];

    /* memset */
    memset(buf, 'a', 8);
    buf[8] = '\0';
    CHECK_EQI((long)strlen(buf), 8, "memset + strlen: length 8");
    CHECK(buf[0] == 'a' && buf[7] == 'a', "memset filled the range");

    /* memcpy */
    memcpy(buf, "hello", 6);
    CHECK_STR(buf, "hello", "memcpy copies bytes");

    /* memmove with overlapping ranges (must not corrupt) */
    memmove(buf + 1, buf, 5);
    buf[6] = '\0';
    CHECK_STR(buf, "hhello", "memmove handles overlap");

    /* memcmp */
    CHECK(memcmp("abc", "abc", 3) == 0, "memcmp equal");
    CHECK(memcmp("abc", "abd", 3) < 0, "memcmp less-than");
    CHECK(memcmp("abd", "abc", 3) > 0, "memcmp greater-than");

    /* memchr */
    const char *hay = "find the x here";
    CHECK(memchr(hay, 'x', strlen(hay)) == hay + 9, "memchr finds a byte");
    CHECK(memchr(hay, 'z', strlen(hay)) == NULL, "memchr miss returns NULL");

    /* strchr / strrchr (operate on a local array so pointer identity is well-defined) */
    char s[] = "a.b.c";
    CHECK(strchr(s, '.') == s + 1, "strchr finds first '.'");
    CHECK(strchr(s, 'c') == s + 4, "strchr finds last char");
    CHECK(strchr(s, 'z') == NULL, "strchr miss returns NULL");
    CHECK(strchr(s, '\0') == s + 5, "strchr matches the terminator");
    CHECK(strrchr(s, '.') == s + 3, "strrchr finds last '.'");

    CHECK_DONE("cstring");
    return 0;
}
