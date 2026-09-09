/* <bit> bit-manipulation + <numbers> math constants (C++20). */
#include "rxdk_test.h"
#include <bit>
#include <numbers>
#include <cstdint>

static int near(double a, double b) { double d = a - b; if (d < 0) d = -d; return d < 1e-9; }

int main(void) {
    CHECK_EQI((long)std::popcount(0xF0F0u), 8, "popcount");
    CHECK_EQI((long)std::countl_zero((uint32_t)1), 31, "countl_zero");
    CHECK_EQI((long)std::countr_zero((uint32_t)8), 3, "countr_zero");
    CHECK(std::has_single_bit(64u), "has_single_bit(64)");
    CHECK(!std::has_single_bit(63u), "has_single_bit(63) false");
    CHECK_EQI((long)std::bit_width(0xFFu), 8, "bit_width");
    CHECK_EQI((long)std::bit_ceil(100u), 128, "bit_ceil");
    CHECK_EQI((long)std::rotl((uint8_t)0b00010000, 2), 0b01000000, "rotl");

    float f = 1.0f;
    uint32_t bits = std::bit_cast<uint32_t>(f);
    CHECK_EQI((long)bits, 0x3F800000L, "bit_cast float->uint32 (1.0f)");

    CHECK(near(std::numbers::pi, 3.14159265358979), "numbers::pi");
    CHECK(near(std::numbers::e, 2.71828182845905), "numbers::e");
    CHECK(near(std::numbers::sqrt2, 1.41421356237310), "numbers::sqrt2");

    CHECK(std::endian::native == std::endian::big, "endian::native is big (Xbox 360)");

    CHECK_DONE("bit");
    return 0;
}
