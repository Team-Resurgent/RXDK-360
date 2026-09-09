/* std::string core operations. */
#include "rxdk_test.h"
#include <string>

int main() {
    std::string s;
    CHECK(s.empty(), "default construct is empty");

    s = "hello";
    CHECK_EQI(s.size(), 5, "size after assign");
    s += ", world";
    s.append("!");
    CHECK_STR(s.c_str(), "hello, world!", "append operator+ and append()");
    CHECK_EQI(s.size(), 13, "size after appends");

    CHECK_EQI((long)s.find("world"), 7, "find substring");
    CHECK(s.find("xyz") == std::string::npos, "find missing -> npos");

    std::string sub = s.substr(7, 5);
    CHECK_STR(sub.c_str(), "world", "substr");

    CHECK(std::string("abc") < std::string("abd"), "operator< lexicographic");
    CHECK(std::string("abc") == std::string("abc"), "operator== equal");

    s.replace(0, 5, "HELLO");
    CHECK_STR(s.c_str(), "HELLO, world!", "replace");

    std::string n = std::to_string(-1234);
    CHECK_STR(n.c_str(), "-1234", "to_string(int)");

    CHECK_EQI(std::stoi("2026"), 2026, "stoi");
    CHECK(std::stod("3.5") == 3.5, "stod");

    std::string big(100, 'x');
    CHECK_EQI(big.size(), 100, "fill construct 100");
    CHECK(big.find_first_not_of('x') == std::string::npos, "all x");

    CHECK_DONE("string");
    return 0;
}
