/* <expected> (C++23) + a couple of <type_traits> runtime-observable checks. */
#include "rxdk_test.h"
#include <expected>
#include <type_traits>
#include <string>

static std::expected<int, std::string> parse(bool ok) {
    if (ok) return 42;
    return std::unexpected(std::string("bad"));
}

int main(void) {
    auto good = parse(true);
    CHECK(good.has_value(), "expected has_value");
    CHECK_EQI(*good, 42, "expected value");
    CHECK_EQI(good.value_or(0), 42, "value_or (value)");

    auto bad = parse(false);
    CHECK(!bad.has_value(), "expected error state");
    CHECK_STR(bad.error().c_str(), "bad", "expected error value");
    CHECK_EQI(bad.value_or(-1), -1, "value_or (error)");

    // monadic
    auto doubled = parse(true).transform([](int x){ return x * 2; });
    CHECK_EQI(*doubled, 84, "expected::transform");

    // type_traits (runtime-observed via ::value). Template args carry commas, so
    // compute into locals first -- a bare comma inside CHECK(...) would be read as
    // a macro-argument separator.
    CHECK(std::is_integral<int>::value, "is_integral<int>");
    CHECK(!std::is_integral<double>::value, "!is_integral<double>");
    CHECK(std::is_floating_point<double>::value, "is_floating_point<double>");
    bool remove_const_ok = std::is_same<int, std::remove_const<const int>::type>::value;
    CHECK(remove_const_ok, "remove_const");
    CHECK(std::is_pointer<int*>::value, "is_pointer");
    bool base_of = std::is_base_of<std::exception, std::runtime_error>::value;
    CHECK(base_of, "is_base_of");
    long ct = (long)sizeof(std::common_type<char, int>::type);
    CHECK_EQI(ct, (long)sizeof(int), "common_type<char,int>=int");

    CHECK_DONE("expected");
    return 0;
}
