/* <cmath> double + float, and a few integer helpers. */
#include "rxdk_test.h"
#include <cmath>
#include <cstdlib>

static int near(double a, double b) { double d = a - b; if (d < 0) d = -d; return d < 1e-9; }

int main() {
    CHECK(near(std::sqrt(16.0), 4.0), "sqrt");
    CHECK(near(std::pow(2.0, 10.0), 1024.0), "pow");
    CHECK(near(std::floor(3.7), 3.0), "floor");
    CHECK(near(std::ceil(3.2), 4.0), "ceil");
    CHECK(near(std::fabs(-2.5), 2.5), "fabs");
    CHECK(near(std::fmod(10.0, 3.0), 1.0), "fmod");
    CHECK(near(std::log(std::exp(1.0)), 1.0), "log/exp");
    CHECK(near(std::sin(0.0), 0.0) && near(std::cos(0.0), 1.0), "sin/cos");
    CHECK(near(std::atan2(1.0, 1.0) * 4.0, 3.14159265358979) || std::atan2(1.0,1.0) > 0.78,
          "atan2");
    CHECK(std::isnan(std::nan("")), "isnan(nan)");
    CHECK(std::isinf(1.0 / 0.0 * (std::abs(0)+1)) || std::isinf(HUGE_VAL), "isinf");

    // float variants (libm sf_*)
    CHECK(near(std::sqrt(2.0f), 1.41421356) || std::sqrtf(2.0f) > 1.41f, "sqrtf");
    CHECK(std::ceilf(2.1f) == 3.0f, "ceilf (unordered_map's dependency)");

    // integer helpers from <cstdlib>
    CHECK_EQI(std::abs(-42), 42, "abs(int)");
    CHECK_EQI(std::labs(-100000L), 100000L, "labs");
    ldiv_t q = std::ldiv(17, 5);
    CHECK(q.quot == 3 && q.rem == 2, "ldiv");

    CHECK_DONE("math");
    return 0;
}
