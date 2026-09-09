/* <stdbit.h> -- C23 bit-manipulation utilities (RXDK-provided over the
   __builtin_stdc_* family). Exercises the type-generic macros and a couple of
   the fixed-width functions, plus the endian macros. */
#include "rxdk_test.h"
#include <stdbit.h>
#include <stdint.h>

int main(void) {
    CHECK_EQI((long)stdc_leading_zeros((uint32_t)1), 31, "leading_zeros(u32 1)");
    CHECK_EQI((long)stdc_leading_zeros((uint8_t)1), 7, "leading_zeros(u8 1)");
    CHECK_EQI((long)stdc_trailing_zeros((uint32_t)8), 3, "trailing_zeros(8)");
    CHECK_EQI((long)stdc_leading_ones((uint8_t)0xF0), 4, "leading_ones(0xF0)");
    CHECK_EQI((long)stdc_trailing_ones((uint8_t)0x07), 3, "trailing_ones(0x07)");
    CHECK_EQI((long)stdc_count_ones((uint32_t)0xF0F0u), 8, "count_ones(0xF0F0)");
    CHECK_EQI((long)stdc_count_zeros((uint8_t)0x0F), 4, "count_zeros(u8 0x0F)");
    CHECK_EQI((long)stdc_first_leading_one((uint8_t)0x01), 8, "first_leading_one(0x01)");
    CHECK_EQI((long)stdc_first_trailing_one((uint8_t)0x08), 4, "first_trailing_one(0x08)");
    CHECK_EQI((long)stdc_bit_width((uint32_t)0xFFu), 8, "bit_width(0xFF)");
    CHECK_EQI((long)stdc_bit_ceil((uint32_t)100u), 128, "bit_ceil(100)");
    CHECK_EQI((long)stdc_bit_floor((uint32_t)100u), 64, "bit_floor(100)");
    CHECK(stdc_has_single_bit((uint32_t)64u), "has_single_bit(64)");
    CHECK(!stdc_has_single_bit((uint32_t)63u), "has_single_bit(63) false");

    /* fixed-width function forms resolve and agree with the generic macros */
    CHECK_EQI((long)stdc_leading_zeros_ui(1u), 31, "leading_zeros_ui function form");
    CHECK_EQI((long)stdc_bit_ceil_us((unsigned short)100), 128, "bit_ceil_us function form");

    /* endian macros: Xbox 360 is big-endian */
    CHECK(__STDC_ENDIAN_NATIVE__ == __STDC_ENDIAN_BIG__, "native endian is big");
    CHECK(__STDC_ENDIAN_LITTLE__ != __STDC_ENDIAN_BIG__, "little != big");

    CHECK_DONE("stdbit");
    return 0;
}
