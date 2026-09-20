/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * RXDK-360 VMX/PPC intrinsic shim for the modern clang toolchain.
 *
 * The stock XDK headers use a handful of Microsoft PowerPC/VMX128 intrinsics
 * that clang does not provide under the MS spellings (they surface as
 * unresolved externals when linking XDK/ATG code). We implement them here with
 * portable, spec-faithful byte semantics - correct on the big-endian PPC target,
 * at a scalar cost (matches the _XM_NO_INTRINSICS_ math path). These have C
 * linkage, matching the XDK header declarations.
 *
 * References: PowerPC VMX (AltiVec) instruction semantics for lvlx/lvrx/stvewx/
 * vor/vslo, and the Xenon dcbz128 cache op.
 */
#include <stdint.h>

/* Mirror the XDK's __vector4 (vectorintrinsics.h): a 16-byte, 16-aligned union.
 * Kept layout-compatible so the ABI matches the XDK-header prototypes. */
typedef struct __attribute__((aligned(16))) __vector4 {
    union {
        float        vector4_f32[4];
        unsigned int vector4_u32[4];
    };
} __vector4;

/* Load Vector Left Indexed: EA = base+offset. Load the bytes from EA up to the
 * next 16-byte boundary, left-justified into the register; the rest are zero.
 * (Paired with __lvrx + __vor this performs an unaligned 16-byte load.) */
__vector4 __lvlx(const void* base, int offset)
{
    uintptr_t ea = (uintptr_t)base + (uintptr_t)offset;
    unsigned n = 16u - (unsigned)(ea & 15u);     /* bytes to the boundary */
    const unsigned char* p = (const unsigned char*)ea;
    __vector4 r;
    unsigned char* o = (unsigned char*)&r;
    for (unsigned i = 0; i < 16u; ++i) o[i] = (i < n) ? p[i] : 0;
    return r;
}

/* Load Vector Right Indexed: EA = base+offset. Load the bytes from the previous
 * 16-byte boundary up to EA, right-justified into the register; rest zero. */
__vector4 __lvrx(const void* base, int offset)
{
    uintptr_t ea = (uintptr_t)base + (uintptr_t)offset;
    unsigned n = (unsigned)(ea & 15u);           /* bytes since the boundary */
    const unsigned char* p = (const unsigned char*)(ea - n);
    __vector4 r;
    unsigned char* o = (unsigned char*)&r;
    for (unsigned i = 0; i < 16u; ++i) o[i] = (i >= 16u - n) ? p[i - (16u - n)] : 0;
    return r;
}

/* Store Vector Element Word Indexed: store the word element selected by EA's
 * bits [2:3] to the word-aligned address at EA. */
void __stvewx(__vector4 vSrc, void* base, int offset)
{
    uintptr_t ea = (uintptr_t)base + (uintptr_t)offset;
    unsigned widx = (unsigned)((ea >> 2) & 3u);
    *(unsigned int*)(ea & ~(uintptr_t)3) = vSrc.vector4_u32[widx];
}

/* Vector logical OR. */
__vector4 __vor(__vector4 a, __vector4 b)
{
    __vector4 r;
    for (unsigned i = 0; i < 4u; ++i) r.vector4_u32[i] = a.vector4_u32[i] | b.vector4_u32[i];
    return r;
}

/* Vector Shift Left by Octet: shift VRA left by sh bytes, where sh is bits
 * [121:124] of VRB (i.e. (low byte >> 3) & 0xF); zero-fill the low bytes. */
__vector4 __vslo(__vector4 a, __vector4 b)
{
    const unsigned char* bb = (const unsigned char*)&b;
    unsigned sh = (unsigned)((bb[15] >> 3) & 0xF);
    const unsigned char* ab = (const unsigned char*)&a;
    __vector4 r;
    unsigned char* o = (unsigned char*)&r;
    for (unsigned i = 0; i < 16u; ++i) o[i] = (i + sh < 16u) ? ab[i + sh] : 0;
    return r;
}

/* Data Cache Block Zero (128-byte line): zero the 128 bytes of the cache line
 * containing base+offset. */
void __dcbz128(int offset, void* base)
{
    uintptr_t a = ((uintptr_t)base + (uintptr_t)offset) & ~(uintptr_t)127;
    unsigned char* p = (unsigned char*)a;
    for (unsigned i = 0; i < 128u; ++i) p[i] = 0;
}

/* Count leading zeros of a 64-bit value (returns 64 for zero). */
unsigned int _CountLeadingZeros64(long long val)
{
    return val ? (unsigned int)__builtin_clzll((unsigned long long)val) : 64u;
}
