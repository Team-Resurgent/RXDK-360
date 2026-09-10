/* C++23 language features (asserted at runtime) + library additions that live in
   already-present headers: optional monadic ops, string::contains /
   resize_and_overwrite, <flat_map>, <flat_set>, <mdspan>, to_underlying,
   byteswap. */
#include "rxdk_test.h"
#include <optional>
#include <string>
#include <flat_map>
#include <flat_set>
#include <mdspan>
#include <utility>   // to_underlying, forward_like
#include <bit>       // byteswap
#include <array>
#include <cstdint>

// if consteval: 1 in a constant-evaluated context, 2 at run time.
static constexpr int ce() { if consteval { return 1; } else { return 2; } }
// deducing this
struct Boxed { int x; int get(this const Boxed &self) { return self.x; } };
// multidimensional subscript
struct Grid { int operator[](int a, int b) const { return a * 10 + b; } };
// static operator()
struct Inc { static int operator()(int v) { return v + 1; } };

int main() {
    // --- language ---
    constexpr int c = ce();
    CHECK_EQI(c, 1, "if consteval: constant-evaluated branch");
    CHECK_EQI(ce(), 2, "if consteval: runtime branch");

    Boxed b{42};
    CHECK_EQI(b.get(), 42, "deducing this (explicit object param)");

    Grid g;
    int gv = g[2, 3];   // the ',' can't sit inside a macro arg
    CHECK_EQI(gv, 23, "multidimensional operator[]");

    CHECK_EQI(Inc{}(5), 6, "static operator()");

    int xv = 5;
    auto yv = auto(xv);   // decay-copy
    yv = 99;
    CHECK(xv == 5 && yv == 99, "auto(x) decay-copy is independent");

    // --- <utility> / <bit> ---
    enum class Color : int { Red = 7 };
    CHECK_EQI(std::to_underlying(Color::Red), 7, "std::to_underlying");
    CHECK_EQI((long)std::byteswap((uint32_t)0x12345678u), (long)0x78563412u, "std::byteswap u32");
    CHECK((int)std::byteswap((uint16_t)0x1234u) == 0x3412, "std::byteswap u16");

    // --- optional monadic ops (C++23) ---
    std::optional<int> o{10};
    auto r = o.and_then([](int v) { return std::optional<int>{v + 1}; })
              .transform([](int v) { return v * 2; });
    CHECK(r.has_value() && *r == 22, "optional and_then + transform");
    std::optional<int> empty;
    auto r2 = empty.or_else([] { return std::optional<int>{99}; });
    CHECK(r2.has_value() && *r2 == 99, "optional or_else on empty");
    auto r3 = o.transform([](int v) { return v + 5; });
    CHECK_EQI(*r3, 15, "optional transform");

    // --- string additions ---
    std::string s = "hello world";
    CHECK(s.contains("world"), "string::contains(substr)");
    CHECK(s.contains('h'), "string::contains(char)");
    CHECK(!s.contains("xyz"), "string::contains false");
    std::string ro;
    ro.resize_and_overwrite(4, [](char *p, size_t n) {
        for (size_t i = 0; i < n; ++i) p[i] = (char)('A' + i);
        return n;
    });
    CHECK(ro == "ABCD", "string::resize_and_overwrite");

    // --- <flat_map> / <flat_set> ---
    std::flat_map<int, std::string> fm;
    fm.insert({3, "three"});
    fm.insert({1, "one"});
    fm.insert({2, "two"});
    CHECK_EQI((long)fm.size(), 3, "flat_map size");
    CHECK(fm.at(2) == "two", "flat_map at()");
    // flat_map keeps keys sorted
    int prev = -1, ordered = 1;
    for (auto &&[k, v] : fm) { (void)v; if (k < prev) ordered = 0; prev = k; }
    CHECK(ordered, "flat_map iterates in sorted key order");

    std::flat_set<int> fs;
    fs.insert(5); fs.insert(1); fs.insert(5); fs.insert(3);
    CHECK_EQI((long)fs.size(), 3, "flat_set dedups");
    CHECK(fs.contains(3) && !fs.contains(9), "flat_set contains");
    CHECK_EQI(*fs.begin(), 1, "flat_set sorted (min first)");

    // --- <mdspan> ---
    std::array<int, 6> storage{10, 11, 12, 13, 14, 15};
    std::mdspan md(storage.data(), 2, 3);
    CHECK_EQI((long)md.extent(0), 2, "mdspan extent(0)");
    CHECK_EQI((long)md.extent(1), 3, "mdspan extent(1)");
    int mv = md[1, 2];
    CHECK_EQI(mv, 15, "mdspan element access [1,2]");

    CHECK_DONE("cxx23");
    return 0;
}
