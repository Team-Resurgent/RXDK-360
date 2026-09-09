/* <chrono> clocks + std::this_thread::sleep_for. */
#include "rxdk_test.h"
#include <chrono>
#include <thread>

int main() {
    using namespace std::chrono;

    auto t0 = steady_clock::now();
    std::this_thread::sleep_for(milliseconds(60));
    auto t1 = steady_clock::now();
    long ms = (long)duration_cast<milliseconds>(t1 - t0).count();
    CHECK(ms >= 55, "sleep_for(60ms): steady_clock elapsed >= 55ms");
    CHECK(ms < 500, "sleep_for(60ms): elapsed not absurdly long");

    CHECK(steady_clock::now() >= t1, "steady_clock monotonic non-decreasing");

    auto secs = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    CHECK(secs > 1500000000LL, "system_clock is a plausible post-2017 wall time");

    auto d = hours(1) + minutes(30);
    CHECK_EQI(duration_cast<minutes>(d).count(), 90, "duration arithmetic 1h30m=90m");

    CHECK_DONE("chrono");
    return 0;
}
