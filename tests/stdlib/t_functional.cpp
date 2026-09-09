/* <functional> -- bind, mem_fn, ref, invoke, hash, and callables. */
#include "rxdk_test.h"
#include <functional>
#include <string>

static int add(int a, int b) { return a + b; }
struct Obj { int v; int get() const { return v; } int add(int x) const { return v + x; } };

int main(void) {
    using namespace std::placeholders;
    auto add5 = std::bind(add, 5, _1);
    CHECK_EQI(add5(10), 15, "bind with placeholder");
    auto swapped = std::bind(add, _2, _1);
    CHECK_EQI(swapped(3, 100), 103, "bind reorders args");

    Obj o{42};
    auto getter = std::mem_fn(&Obj::get);
    CHECK_EQI(getter(o), 42, "mem_fn");
    auto adder = std::mem_fn(&Obj::add);
    CHECK_EQI(adder(o, 8), 50, "mem_fn with arg");

    CHECK_EQI(std::invoke(add, 2, 3), 5, "invoke free function");
    CHECK_EQI(std::invoke(&Obj::get, o), 42, "invoke member function");

    int x = 10;
    auto rw = std::ref(x);
    rw.get() = 20;
    CHECK_EQI(x, 20, "reference_wrapper writes through");

    std::function<int(int)> f = [](int n){ return n * n; };
    CHECK_EQI(f(7), 49, "std::function lambda");
    f = add5;
    CHECK_EQI(f(1), 6, "std::function reassign to bind");

    CHECK(std::hash<std::string>{}("a") != std::hash<std::string>{}("b"), "std::hash<string> differs");
    CHECK_EQI((long)(std::plus<int>{}(4, 5)), 9, "std::plus");
    CHECK(std::greater<int>{}(5, 3), "std::greater");

    CHECK_DONE("functional");
    return 0;
}
