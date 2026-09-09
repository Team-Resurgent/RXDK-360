/* <algorithm> + <numeric> + lambdas / std::function. */
#include "rxdk_test.h"
#include <algorithm>
#include <numeric>
#include <vector>
#include <functional>

int main() {
    std::vector<int> v = {5, 3, 9, 1, 7, 2, 8, 4, 6, 0};
    std::sort(v.begin(), v.end());
    CHECK(std::is_sorted(v.begin(), v.end()), "sort ascending");
    CHECK(v.front() == 0 && v.back() == 9, "sort min/max");

    std::sort(v.begin(), v.end(), std::greater<int>());
    CHECK(v.front() == 9 && v.back() == 0, "sort with comparator");

    CHECK_EQI(std::accumulate(v.begin(), v.end(), 0), 45, "accumulate sum");

    auto it = std::find(v.begin(), v.end(), 7);
    CHECK(it != v.end() && *it == 7, "find");

    int evens = (int)std::count_if(v.begin(), v.end(), [](int x){ return x % 2 == 0; });
    CHECK_EQI(evens, 5, "count_if evens (lambda)");

    std::vector<int> sq(v.size());
    std::transform(v.begin(), v.end(), sq.begin(), [](int x){ return x * x; });
    CHECK(sq[0] == 81, "transform squares");

    CHECK_EQI(*std::max_element(v.begin(), v.end()), 9, "max_element");
    CHECK_EQI(*std::min_element(v.begin(), v.end()), 0, "min_element");

    std::reverse(v.begin(), v.end());
    CHECK(std::is_sorted(v.begin(), v.end()), "reverse of descending -> ascending");

    CHECK(std::binary_search(v.begin(), v.end(), 6), "binary_search hit");
    CHECK(!std::binary_search(v.begin(), v.end(), 42), "binary_search miss");

    std::function<int(int, int)> add = [](int a, int b){ return a + b; };
    CHECK_EQI(add(20, 22), 42, "std::function");

    CHECK_DONE("algorithm");
    return 0;
}
