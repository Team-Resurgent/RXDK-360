/* <bitset> <complex> <numeric> <ratio> <limits>. */
#include "rxdk_test.h"
#include <bitset>
#include <complex>
#include <numeric>
#include <ratio>
#include <limits>
#include <string>

static int near(double a, double b) { double d = a - b; if (d < 0) d = -d; return d < 1e-9; }

int main() {
    std::bitset<8> bs(0b10110010);
    CHECK_EQI((long)bs.count(), 4, "bitset count");
    CHECK(bs.test(1) && !bs.test(0), "bitset test");
    bs.set(0);
    CHECK(bs.test(0), "bitset set");
    CHECK_STR(bs.to_string().c_str(), "10110011", "bitset to_string");

    std::complex<double> z(3.0, 4.0);
    CHECK(near(std::abs(z), 5.0), "complex abs (3,4)->5");
    CHECK(near(z.real(), 3.0) && near(z.imag(), 4.0), "complex real/imag");
    std::complex<double> w = z * std::complex<double>(0.0, 1.0);
    CHECK(near(w.real(), -4.0) && near(w.imag(), 3.0), "complex multiply by i");

    CHECK_EQI(std::gcd(12, 18), 6, "gcd");
    CHECK_EQI(std::lcm(4, 6), 12, "lcm");

    int arr[5] = {1, 2, 3, 4, 5};
    CHECK_EQI(std::accumulate(arr, arr + 5, 0), 15, "accumulate array");
    CHECK_EQI(std::inner_product(arr, arr + 5, arr, 0), 55, "inner_product");
    int part[5];
    std::partial_sum(arr, arr + 5, part);
    CHECK(part[4] == 15 && part[2] == 6, "partial_sum");

    typedef std::ratio<1, 3> third;
    typedef std::ratio_add<third, third> two_thirds;
    CHECK(two_thirds::num == 2 && two_thirds::den == 3, "ratio_add 1/3+1/3=2/3");

    CHECK_EQI(std::numeric_limits<int>::max(), 2147483647L, "numeric_limits<int>::max");
    CHECK(std::numeric_limits<double>::is_iec559, "double is IEEE 754");
    CHECK_EQI((long)std::numeric_limits<unsigned char>::max(), 255, "uchar max");

    CHECK_DONE("numeric");
    return 0;
}
