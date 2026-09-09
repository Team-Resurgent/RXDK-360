/* <ranges> views + ranges algorithms (C++20). */
#include "rxdk_test.h"
#include <ranges>
#include <algorithm>
#include <vector>
#include <numeric>

int main(void) {
    namespace rv = std::ranges::views;
    std::vector<int> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    // ranges algorithm
    std::ranges::sort(v, std::ranges::greater{});
    CHECK(v.front() == 10 && v.back() == 1, "ranges::sort with comparator");
    std::ranges::sort(v);
    CHECK(std::ranges::is_sorted(v), "ranges::is_sorted");

    auto it = std::ranges::find(v, 7);
    CHECK(it != v.end() && *it == 7, "ranges::find");
    CHECK_EQI((long)std::ranges::count_if(v, [](int x){ return x % 2 == 0; }), 5, "ranges::count_if");

    // views: filter + transform pipeline
    int sum = 0;
    for (int x : v | rv::filter([](int n){ return n % 2 == 0; })
                   | rv::transform([](int n){ return n * n; }))
        sum += x;
    CHECK_EQI(sum, 4 + 16 + 36 + 64 + 100, "filter|transform pipeline");

    // take / drop
    int taken = 0;
    for (int x : v | rv::take(3)) taken += x;
    CHECK_EQI(taken, 1 + 2 + 3, "views::take(3)");

    // iota
    int io = 0;
    for (int x : rv::iota(1, 6)) io += x;
    CHECK_EQI(io, 15, "views::iota(1,6)");

    // reverse
    std::vector<int> rev;
    for (int x : v | rv::reverse) rev.push_back(x);
    CHECK(rev.front() == 10 && rev.back() == 1, "views::reverse");

    CHECK_DONE("ranges");
    return 0;
}
