/* <regex> match / search / replace / captures. */
#include "rxdk_test.h"
#include <regex>
#include <string>

int main() {
    std::regex re("[0-9]+");
    CHECK(std::regex_match(std::string("12345"), re), "regex_match full");
    CHECK(!std::regex_match(std::string("12a45"), re), "regex_match rejects non-match");

    std::smatch m;
    std::string s = "order 42 shipped";
    CHECK(std::regex_search(s, m, re), "regex_search finds number");
    CHECK_STR(m.str().c_str(), "42", "search captured match text");

    std::regex kv("(\\w+)=(\\w+)");
    std::smatch g;
    std::string in = "key=value";
    CHECK(std::regex_search(in, g, kv) && g.size() == 3, "capture groups count");
    CHECK_STR(g[1].str().c_str(), "key", "group 1");
    CHECK_STR(g[2].str().c_str(), "value", "group 2");

    std::string out = std::regex_replace(std::string("a1b2c3"), std::regex("[0-9]"), "#");
    CHECK_STR(out.c_str(), "a#b#c#", "regex_replace");

    CHECK_DONE("regex");
    return 0;
}
