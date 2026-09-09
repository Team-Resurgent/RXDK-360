/* <optional> <variant> <tuple> <any> <utility> -- vocabulary types. */
#include "rxdk_test.h"
#include <optional>
#include <variant>
#include <tuple>
#include <any>
#include <utility>
#include <string>

int main() {
    std::optional<int> o;
    CHECK(!o.has_value(), "optional empty");
    o = 42;
    CHECK(o.has_value() && *o == 42, "optional holds value");
    CHECK_EQI(o.value_or(0), 42, "optional value_or (set)");
    o.reset();
    CHECK_EQI(std::optional<int>().value_or(7), 7, "optional value_or (empty)");

    std::variant<int, std::string, double> v = std::string("hi");
    CHECK_EQI((long)v.index(), 1, "variant index");
    CHECK(std::holds_alternative<std::string>(v), "variant holds_alternative");
    CHECK_STR(std::get<std::string>(v).c_str(), "hi", "variant get");
    v = 3.5;
    CHECK(std::get<double>(v) == 3.5, "variant reassign type");
    int visited = 0;
    std::visit([&](auto &&x){ (void)x; visited = 1; }, v);
    CHECK(visited, "variant visit");

    auto t = std::make_tuple(1, std::string("two"), 3.0);
    CHECK_EQI(std::get<0>(t), 1, "tuple get<0>");
    CHECK_STR(std::get<1>(t).c_str(), "two", "tuple get<1>");
    CHECK(std::tuple_size<decltype(t)>::value == 3, "tuple_size");
    int a; std::string b; double c;
    std::tie(a, b, c) = t;
    CHECK(a == 1 && b == "two" && c == 3.0, "tuple tie unpack");

    std::any any = 123;
    CHECK(any.has_value() && std::any_cast<int>(any) == 123, "any holds int");
    any = std::string("text");
    CHECK_STR(std::any_cast<std::string>(any).c_str(), "text", "any reassign");
    int threw = 0;
    try { std::any_cast<double>(any); } catch (const std::bad_any_cast &) { threw = 1; }
    CHECK(threw, "any_cast wrong type throws");

    auto pr = std::make_pair(1, 2);
    std::swap(pr.first, pr.second);
    CHECK(pr.first == 2 && pr.second == 1, "pair + std::swap");

    CHECK_DONE("utility");
    return 0;
}
