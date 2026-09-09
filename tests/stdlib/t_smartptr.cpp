/* <memory>: shared_ptr / unique_ptr / weak_ptr. */
#include "rxdk_test.h"
#include <memory>
#include <vector>

struct Counter {
    static int live;
    int v;
    Counter(int x) : v(x) { ++live; }
    ~Counter() { --live; }
};
int Counter::live = 0;

int main() {
    CHECK_EQI(Counter::live, 0, "no instances initially");
    {
        auto a = std::make_shared<Counter>(10);
        CHECK_EQI(a->v, 10, "shared_ptr deref");
        CHECK_EQI((long)a.use_count(), 1, "use_count 1");
        auto b = a;
        CHECK_EQI((long)a.use_count(), 2, "use_count 2 after copy");

        std::weak_ptr<Counter> w = a;
        CHECK(!w.expired(), "weak_ptr not expired while shared alive");
        {
            auto locked = w.lock();
            CHECK(locked && locked->v == 10, "weak_ptr::lock");
        }
        b.reset();
        CHECK_EQI((long)a.use_count(), 1, "use_count back to 1 after reset");
        CHECK_EQI(Counter::live, 1, "one instance live");
    }
    CHECK_EQI(Counter::live, 0, "shared_ptr freed at scope end");

    {
        auto u = std::make_unique<Counter>(5);
        CHECK_EQI(u->v, 5, "unique_ptr deref");
        CHECK_EQI(Counter::live, 1, "unique_ptr instance live");
        auto u2 = std::move(u);
        CHECK(!u && u2 && u2->v == 5, "unique_ptr move transfers ownership");
    }
    CHECK_EQI(Counter::live, 0, "unique_ptr freed at scope end");

    std::vector<std::shared_ptr<Counter>> vec;
    for (int i = 0; i < 5; ++i) vec.push_back(std::make_shared<Counter>(i));
    CHECK_EQI(Counter::live, 5, "5 shared in vector");
    vec.clear();
    CHECK_EQI(Counter::live, 0, "vector clear frees all");

    CHECK_DONE("smartptr");
    return 0;
}
