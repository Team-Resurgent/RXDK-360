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

/* Count leading zeros of a 32-bit value (returns 32 for zero). */
unsigned int _CountLeadingZeros(long val)
{
    return val ? (unsigned int)__builtin_clz((unsigned int)val) : 32u;
}

/* ---- scalar PPC intrinsics (ppcintrinsics.h) ---------------------------- */

/* Cache-block hints. On the console these prefetch/flush a cache line; there is
 * nothing to do in a correctness-only model, so they are no-ops. */
void __dcbt(int offset, const void* base) { (void)offset; (void)base; }
void __dcbf(int offset, const void* base) { (void)offset; (void)base; }

/* Move From Time Base: the 64-bit Xenon time base. Read hi/lo/hi with a retry so
 * a low-word wrap between the two reads is not observed. */
unsigned long long __mftb(void)
{
    unsigned int hi, lo, hi2;
    do {
        __asm__ volatile("mftbu %0" : "=r"(hi));
        __asm__ volatile("mftb  %0" : "=r"(lo));
        __asm__ volatile("mftbu %0" : "=r"(hi2));
    } while (hi != hi2);
    return ((unsigned long long)hi << 32) | lo;
}

/* Byte-reversed (little-endian) loads: read the big-endian bytes at base+offset
 * and assemble them low-byte-first. */
unsigned long __loadwordbytereverse(int offset, const void* base)
{
    const unsigned char* p = (const unsigned char*)base + offset;
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8)
         | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}
unsigned short __loadshortbytereverse(int offset, const void* base)
{
    const unsigned char* p = (const unsigned char*)base + offset;
    return (unsigned short)(p[0] | (p[1] << 8));
}
void __storewordbytereverse(unsigned long v, int offset, void* base)
{
    unsigned char* p = (unsigned char*)base + offset;
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}
void __storeshortbytereverse(unsigned short v, int offset, void* base)
{
    unsigned char* p = (unsigned char*)base + offset;
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
}

/* Floating select: fComparand >= 0 ? fValGE : fLT (the fsel instruction). */
double __fsel(double fComparand, double fValGE, double fLT)
{
    return fComparand >= 0.0 ? fValGE : fLT;
}

/* __emit issues a raw instruction word; the XDK uses it only for the memory
 * barriers __sync/__lwsync/__eieio, so a full barrier is the safe model. */
void __emit(unsigned int opcode) { (void)opcode; __sync_synchronize(); }

/* ---- VMX floating-point vector ops (vectorintrinsics.h) ----------------- */

__vector4 __vaddfp(__vector4 a, __vector4 b)
{
    __vector4 r;
    for (unsigned i = 0; i < 4u; ++i) r.vector4_f32[i] = a.vector4_f32[i] + b.vector4_f32[i];
    return r;
}
__vector4 __vsubfp(__vector4 a, __vector4 b)
{
    __vector4 r;
    for (unsigned i = 0; i < 4u; ++i) r.vector4_f32[i] = a.vector4_f32[i] - b.vector4_f32[i];
    return r;
}
__vector4 __vmaddfp(__vector4 a, __vector4 b, __vector4 c)   /* a*b + c */
{
    __vector4 r;
    for (unsigned i = 0; i < 4u; ++i) r.vector4_f32[i] = a.vector4_f32[i] * b.vector4_f32[i] + c.vector4_f32[i];
    return r;
}
__vector4 __vnmsubfp(__vector4 a, __vector4 b, __vector4 c)  /* c - a*b */
{
    __vector4 r;
    for (unsigned i = 0; i < 4u; ++i) r.vector4_f32[i] = c.vector4_f32[i] - a.vector4_f32[i] * b.vector4_f32[i];
    return r;
}
__vector4 __vmsum3fp(__vector4 a, __vector4 b)               /* 3-way dot, broadcast */
{
    float s = a.vector4_f32[0] * b.vector4_f32[0]
            + a.vector4_f32[1] * b.vector4_f32[1]
            + a.vector4_f32[2] * b.vector4_f32[2];
    __vector4 r;
    for (unsigned i = 0; i < 4u; ++i) r.vector4_f32[i] = s;
    return r;
}
__vector4 __vmsum4fp(__vector4 a, __vector4 b)               /* 4-way dot, broadcast */
{
    float s = a.vector4_f32[0] * b.vector4_f32[0]
            + a.vector4_f32[1] * b.vector4_f32[1]
            + a.vector4_f32[2] * b.vector4_f32[2]
            + a.vector4_f32[3] * b.vector4_f32[3];
    __vector4 r;
    for (unsigned i = 0; i < 4u; ++i) r.vector4_f32[i] = s;
    return r;
}
