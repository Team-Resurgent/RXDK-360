/* <format> (C++20/23). */
#include "rxdk_test.h"
#include <format>
#include <string>

int main() {
    CHECK_STR(std::format("{} + {} = {}", 2, 3, 5).c_str(), "2 + 3 = 5", "format positional");
    CHECK_STR(std::format("{0}{1}{0}", "a", "b").c_str(), "aba", "format arg index reuse");
    CHECK_STR(std::format("{:05d}", 42).c_str(), "00042", "format width/fill");
    CHECK_STR(std::format("{:x}", 255).c_str(), "ff", "format hex");
    CHECK_STR(std::format("{:.2f}", 3.14159).c_str(), "3.14", "format float precision");
    CHECK_STR(std::format("{:>6}", "hi").c_str(), "    hi", "format right align");
    CHECK_STR(std::format("{}", std::string("str")).c_str(), "str", "format std::string");
    CHECK_DONE("format");
    return 0;
}
