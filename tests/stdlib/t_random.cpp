/* <random> engines + distributions. */
#include "rxdk_test.h"
#include <random>

int main() {
    std::mt19937 a(12345), b(12345);
    CHECK(a() == b(), "mt19937 deterministic for same seed");
    // Known value: mt19937 seeded with 5489 (default), first output is 3499211612.
    std::mt19937 def;
    CHECK(def() == 3499211612u, "mt19937 default-seed first output (known vector)");

    std::mt19937 e(42);
    std::uniform_int_distribution<int> di(1, 6);
    int inrange = 1, seen[7] = {0};
    for (int i = 0; i < 2000; ++i) { int r = di(e); if (r < 1 || r > 6) inrange = 0; seen[r]++; }
    CHECK(inrange, "uniform_int_distribution stays in [1,6]");
    int all_seen = 1; for (int f = 1; f <= 6; ++f) if (!seen[f]) all_seen = 0;
    CHECK(all_seen, "uniform_int covers all faces");

    std::uniform_real_distribution<double> dr(0.0, 1.0);
    int rok = 1; for (int i = 0; i < 1000; ++i) { double x = dr(e); if (x < 0.0 || x >= 1.0) rok = 0; }
    CHECK(rok, "uniform_real_distribution in [0,1)");

    std::minstd_rand lc(1);
    CHECK(lc() != 0, "minstd_rand produces output");

    std::bernoulli_distribution bd(0.5);
    int trues = 0; for (int i = 0; i < 1000; ++i) if (bd(e)) trues++;
    CHECK(trues > 300 && trues < 700, "bernoulli ~50% over 1000");

    CHECK_DONE("random");
    return 0;
}
