/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * <stdbit.h> -- C23 bit-manipulation utilities (7.18). picolibc's snapshot
 * predates this header, and clang 24 ships the __builtin_stdc_* family but not
 * the header, so RXDK supplies it here (runtime/config is on the C include path
 * for every clang title). Each operation is the type-generic macro C23 mandates
 * plus the five fixed-type functions (uc/us/ui/ul/ull); both map straight onto
 * the compiler builtins, which already pick the width and return type.
 */
#ifndef _STDBIT_H
#define _STDBIT_H

#define __STDC_VERSION_STDBIT_H__ 202311L

/* Endianness (7.18.2). Xbox 360 is big-endian; keep it derived from the
   compiler's own macros rather than hard-coded, so this header stays correct if
   reused for another target. */
#define __STDC_ENDIAN_LITTLE__ __ORDER_LITTLE_ENDIAN__
#define __STDC_ENDIAN_BIG__    __ORDER_BIG_ENDIAN__
#define __STDC_ENDIAN_NATIVE__ __BYTE_ORDER__

#ifdef __cplusplus
extern "C" {
#endif

/* count/index/width family: return unsigned int, one per fixed width. */
#define __RXDK_STDBIT_UINT(op)                                                 \
    static inline unsigned int stdc_##op##_uc(unsigned char __v)               \
        { return __builtin_stdc_##op(__v); }                                   \
    static inline unsigned int stdc_##op##_us(unsigned short __v)              \
        { return __builtin_stdc_##op(__v); }                                   \
    static inline unsigned int stdc_##op##_ui(unsigned int __v)                \
        { return __builtin_stdc_##op(__v); }                                   \
    static inline unsigned int stdc_##op##_ul(unsigned long __v)               \
        { return __builtin_stdc_##op(__v); }                                   \
    static inline unsigned int stdc_##op##_ull(unsigned long long __v)         \
        { return __builtin_stdc_##op(__v); }

__RXDK_STDBIT_UINT(leading_zeros)
__RXDK_STDBIT_UINT(leading_ones)
__RXDK_STDBIT_UINT(trailing_zeros)
__RXDK_STDBIT_UINT(trailing_ones)
__RXDK_STDBIT_UINT(first_leading_zero)
__RXDK_STDBIT_UINT(first_leading_one)
__RXDK_STDBIT_UINT(first_trailing_zero)
__RXDK_STDBIT_UINT(first_trailing_one)
__RXDK_STDBIT_UINT(count_zeros)
__RXDK_STDBIT_UINT(count_ones)
__RXDK_STDBIT_UINT(bit_width)

/* has_single_bit: returns bool. */
#define __RXDK_STDBIT_BOOL(op)                                                 \
    static inline _Bool stdc_##op##_uc(unsigned char __v)                      \
        { return __builtin_stdc_##op(__v); }                                   \
    static inline _Bool stdc_##op##_us(unsigned short __v)                     \
        { return __builtin_stdc_##op(__v); }                                   \
    static inline _Bool stdc_##op##_ui(unsigned int __v)                       \
        { return __builtin_stdc_##op(__v); }                                   \
    static inline _Bool stdc_##op##_ul(unsigned long __v)                      \
        { return __builtin_stdc_##op(__v); }                                   \
    static inline _Bool stdc_##op##_ull(unsigned long long __v)                \
        { return __builtin_stdc_##op(__v); }

__RXDK_STDBIT_BOOL(has_single_bit)

/* bit_floor / bit_ceil: return the argument's own type. */
#define __RXDK_STDBIT_SAME(op, ty, suf)                                        \
    static inline ty stdc_##op##_##suf(ty __v)                                 \
        { return (ty)__builtin_stdc_##op(__v); }
#define __RXDK_STDBIT_SAME_ALL(op)                                             \
    __RXDK_STDBIT_SAME(op, unsigned char, uc)                                  \
    __RXDK_STDBIT_SAME(op, unsigned short, us)                                 \
    __RXDK_STDBIT_SAME(op, unsigned int, ui)                                   \
    __RXDK_STDBIT_SAME(op, unsigned long, ul)                                  \
    __RXDK_STDBIT_SAME(op, unsigned long long, ull)

__RXDK_STDBIT_SAME_ALL(bit_floor)
__RXDK_STDBIT_SAME_ALL(bit_ceil)

#undef __RXDK_STDBIT_UINT
#undef __RXDK_STDBIT_BOOL
#undef __RXDK_STDBIT_SAME
#undef __RXDK_STDBIT_SAME_ALL

/* Type-generic macros (7.18.x): the builtins are already generic over any
   unsigned integer type and yield the correct return type per operation. */
#define stdc_leading_zeros(x)       __builtin_stdc_leading_zeros(x)
#define stdc_leading_ones(x)        __builtin_stdc_leading_ones(x)
#define stdc_trailing_zeros(x)      __builtin_stdc_trailing_zeros(x)
#define stdc_trailing_ones(x)       __builtin_stdc_trailing_ones(x)
#define stdc_first_leading_zero(x)  __builtin_stdc_first_leading_zero(x)
#define stdc_first_leading_one(x)   __builtin_stdc_first_leading_one(x)
#define stdc_first_trailing_zero(x) __builtin_stdc_first_trailing_zero(x)
#define stdc_first_trailing_one(x)  __builtin_stdc_first_trailing_one(x)
#define stdc_count_zeros(x)         __builtin_stdc_count_zeros(x)
#define stdc_count_ones(x)          __builtin_stdc_count_ones(x)
#define stdc_has_single_bit(x)      __builtin_stdc_has_single_bit(x)
#define stdc_bit_width(x)           __builtin_stdc_bit_width(x)
#define stdc_bit_floor(x)           __builtin_stdc_bit_floor(x)
#define stdc_bit_ceil(x)            __builtin_stdc_bit_ceil(x)

#ifdef __cplusplus
}
#endif

#endif /* _STDBIT_H */
