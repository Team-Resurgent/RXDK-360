/* views::cartesian_product (C++23, P2374) -- implemented in the RXDK-360 llvm
   fork (upstream libc++ has not shipped it). */
#include "rxdk_test.h"
#include <ranges>
#include <vector>
#include <string>
#include <tuple>

int main() {
    std::vector<int> a{1, 2, 3};
    std::vector<char> b{'x', 'y'};

    // 3 x 2 = 6 combinations, in row-major (last range varies fastest).
    {
        int count = 0, sum_i = 0;
        std::string seq;
        for (auto&& [i, c] : std::views::cartesian_product(a, b)) {
            ++count;
            sum_i += i;
            seq += c;
        }
        CHECK_EQI(count, 6, "3 x 2 -> 6 tuples");
        CHECK_EQI(sum_i, (1 + 2 + 3) * 2, "each a-value appears twice");
        CHECK(seq == "xyxyxy", "last range varies fastest (row-major)");
    }

    // size() is the product
    {
        auto v = std::views::cartesian_product(a, b);
        CHECK_EQI((long)v.size(), 6, "size() is the product of sizes");
    }

    // first combination is (a[0], b[0])
    {
        auto v      = std::views::cartesian_product(a, b);
        auto [i, c] = *v.begin();
        CHECK(i == 1 && c == 'x', "first tuple is (1,'x')");
    }

    // three-way product 2x2x2 = 8
    {
        std::vector<int> x{0, 1}, y{0, 1}, z{0, 1};
        int n = 0, ones = 0;
        for (auto&& [i, j, k] : std::views::cartesian_product(x, y, z)) {
            ++n;
            ones += (i + j + k);
        }
        CHECK_EQI(n, 8, "2x2x2 -> 8 tuples");
        CHECK_EQI(ones, 12, "sum over all bits: each position set in half of 8 = 4, x3 = 12");
    }

    // empty factor -> empty product
    {
        std::vector<int> empty;
        int n = 0;
        for (auto&& t : std::views::cartesian_product(a, empty)) { (void)t; ++n; }
        CHECK_EQI(n, 0, "an empty factor makes the product empty");
    }

    CHECK_DONE("cartesian");
    return 0;
}
