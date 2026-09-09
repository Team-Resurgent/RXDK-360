/* <string_view> -- non-owning string references. */
#include "rxdk_test.h"
#include <string_view>
#include <string>

int main(void) {
    std::string_view sv = "hello world";
    CHECK_EQI((long)sv.size(), 11, "size");
    CHECK(sv.substr(0, 5) == "hello", "substr");
    CHECK(sv.starts_with("hello"), "starts_with");
    CHECK(sv.ends_with("world"), "ends_with");
    CHECK(sv.contains("lo w"), "contains");
    CHECK_EQI((long)sv.find("world"), 6, "find");
    CHECK(sv.find("xyz") == std::string_view::npos, "find missing npos");

    std::string owner = "constructed";
    std::string_view from_str = owner;
    CHECK(from_str == "constructed", "from std::string");
    CHECK(from_str.data() == owner.data(), "no copy (shares buffer)");

    std::string_view r = "  trim  ";
    r.remove_prefix(2);
    r.remove_suffix(2);
    CHECK(r == "trim", "remove_prefix/suffix");

    CHECK_DONE("string_view");
    return 0;
}
