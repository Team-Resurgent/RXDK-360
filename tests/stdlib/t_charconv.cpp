/* <charconv> to_chars / from_chars (locale-independent, allocation-free). */
#include "rxdk_test.h"
#include <charconv>
#include <string>
#include <cstring>

int main(void) {
    char buf[64];

    auto r = std::to_chars(buf, buf + sizeof buf, 12345);
    *r.ptr = '\0';
    CHECK(r.ec == std::errc{}, "to_chars int ok");
    CHECK_STR(buf, "12345", "to_chars int value");

    r = std::to_chars(buf, buf + sizeof buf, 255, 16);
    *r.ptr = '\0';
    CHECK_STR(buf, "ff", "to_chars hex base");

    r = std::to_chars(buf, buf + sizeof buf, -42);
    *r.ptr = '\0';
    CHECK_STR(buf, "-42", "to_chars negative");

    r = std::to_chars(buf, buf + sizeof buf, 3.14);
    *r.ptr = '\0';
    CHECK(std::strncmp(buf, "3.14", 4) == 0, "to_chars double");

    int iv = 0;
    const char *s = "6789rest";
    auto fr = std::from_chars(s, s + std::strlen(s), iv);
    CHECK_EQI(iv, 6789, "from_chars int value");
    CHECK(*fr.ptr == 'r', "from_chars stops at non-digit");

    double dv = 0;
    const char *ds = "2.5xyz";
    auto fd = std::from_chars(ds, ds + std::strlen(ds), dv);
    CHECK(dv == 2.5, "from_chars double value");
    CHECK(fd.ec == std::errc{}, "from_chars double ok");

    CHECK_DONE("charconv");
    return 0;
}
