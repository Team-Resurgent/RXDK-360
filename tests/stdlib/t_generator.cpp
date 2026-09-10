/* std::generator (C++23) -- coroutine range that yields values. Implemented in
   the RXDK-360 llvm fork (upstream libc++ has not shipped it). */
#include "rxdk_test.h"
#include <generator>
#include <string>
#include <vector>
#include <ranges>
#include <algorithm>

static std::generator<int> squares(int n) {
    for (int i = 0; i < n; ++i)
        co_yield i * i;
}

static std::generator<int> fib(int n) {
    int a = 0, b = 1;
    for (int i = 0; i < n; ++i) { co_yield a; int t = a + b; a = b; b = t; }
}

// yields an object type (exercises the value-copy path)
static std::generator<std::string> words() {
    std::string s = "ab";
    co_yield s;            // lvalue -> copied into the generator
    co_yield "cd";         // rvalue
    co_yield s + "!";
}

// yields references into a container (no copies)
static std::generator<int&> each(std::vector<int>& v) {
    for (int& x : v)
        co_yield x;
}

int main() {
    // range-for over a value generator
    int sum = 0, count = 0;
    for (int x : squares(5)) { sum += x; ++count; }
    CHECK_EQI(sum, 30, "sum of squares 0..4 == 30");
    CHECK_EQI(count, 5, "generator produced 5 items");

    // fibonacci
    std::vector<int> f;
    for (int x : fib(8)) f.push_back(x);
    CHECK(f.size() == 8 && f[7] == 13, "fib(8) last is 13");

    // object generator (lvalue + rvalue + expression yields)
    std::vector<std::string> ws;
    for (auto&& w : words()) ws.push_back(w);
    CHECK(ws.size() == 3 && ws[0] == "ab" && ws[1] == "cd" && ws[2] == "ab!",
          "string generator yields lvalue/rvalue/expr");

    // reference generator mutates the underlying container
    std::vector<int> data{1, 2, 3};
    for (int& r : each(data)) r *= 10;
    CHECK(data[0] == 10 && data[1] == 20 && data[2] == 30, "reference generator mutates in place");

    // composes with ranges algorithms/views
    auto g = squares(6);
    int evens = 0;
    for (int x : g) if (x % 2 == 0) ++evens;   // 0,4,16 -> 3
    CHECK_EQI(evens, 3, "generator usable as an input range");

    CHECK_DONE("generator");
    return 0;
}
