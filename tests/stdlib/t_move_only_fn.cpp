/* std::move_only_function (C++23) -- implemented in the RXDK-360 llvm fork's
   libc++ (upstream libc++ has not shipped it). Move-only, type-erased callable. */
#include "rxdk_test.h"
#include <functional>
#include <memory>
#include <string>

static int add(int a, int b) { return a + b; }
struct MofCounter { static int live; MofCounter() { ++live; } ~MofCounter() { --live; } MofCounter(MofCounter&&){ ++live; } };
int MofCounter::live = 0;

int main() {
    // from a plain function
    std::move_only_function<int(int, int)> f = add;
    CHECK(bool(f), "constructed is non-empty");
    CHECK_EQI(f(2, 3), 5, "call free function");

    // move-only target (unique_ptr capture) -- the whole point vs std::function
    auto up = std::make_unique<int>(40);
    std::move_only_function<int()> g = [p = std::move(up)] { return *p + 2; };
    CHECK_EQI(g(), 42, "call move-only (unique_ptr) lambda");

    // move construction transfers ownership; source becomes empty
    std::move_only_function<int()> g2 = std::move(g);
    CHECK(!g, "moved-from is empty");
    CHECK_EQI(g2(), 42, "moved-to still callable");

    // const-qualified signature
    std::move_only_function<int() const> h = [] { return 7; };
    CHECK_EQI(h(), 7, "const-qualified call");

    // noexcept-qualified signature
    std::move_only_function<int() noexcept> n = []() noexcept { return 9; };
    CHECK_EQI(n(), 9, "noexcept-qualified call");

    // nullptr / empty / reassignment
    std::move_only_function<int(int, int)> e;
    CHECK(!e, "default-constructed is empty");
    CHECK(e == nullptr, "compares equal to nullptr when empty");
    f = nullptr;
    CHECK(!f, "assigned nullptr is empty");
    f = [](int a, int b) { return a * b; };
    CHECK_EQI(f(6, 7), 42, "reassigned to a new callable");

    // ref-qualified (&&): callable once on an rvalue
    std::move_only_function<int() &&> r = [] { return 100; };
    CHECK_EQI(std::move(r)(), 100, "rvalue-ref-qualified call");

    // destroys the owned target (no leak of the wrapped object)
    {
        std::move_only_function<void()> c = [x = MofCounter{}] { (void)x; };
        CHECK_EQI(MofCounter::live, 1, "target held alive by the wrapper");
    }
    CHECK_EQI(MofCounter::live, 0, "target destroyed with the wrapper");

    CHECK_DONE("move_only_fn");
    return 0;
}
