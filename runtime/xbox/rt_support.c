/*
 * Low-level runtime support the modern libc needs that is neither picolibc nor
 * a kernel import:
 *
 *  - the 64-bit integer division helpers the compiler emits on this 32-bit
 *    target (picolibc's integer formatting divides 64-bit values), and
 *  - a temporary allocator so malloc/free resolve.
 *
 * The division helpers use plain binary long division (only shifts, compares and
 * subtracts -- no 64-bit divide -- so with -fno-builtin they never recurse into
 * __udivdi3). The allocator is a placeholder bump allocator; the real malloc
 * will come from the console heap (RtlAllocateHeap) like RXDK-Libs' heapalloc.c.
 */
#include <stddef.h>

/* ---- 64-bit division (compiler-rt equivalents) --------------------------- */

unsigned long long __udivmoddi4(unsigned long long n, unsigned long long d,
                                unsigned long long *rem) {
    unsigned long long q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1ULL);
        if (r >= d) {
            r -= d;
            q |= (1ULL << i);
        }
    }
    if (rem)
        *rem = r;
    return q;
}

unsigned long long __udivdi3(unsigned long long a, unsigned long long b) {
    return __udivmoddi4(a, b, 0);
}

unsigned long long __umoddi3(unsigned long long a, unsigned long long b) {
    unsigned long long r;
    __udivmoddi4(a, b, &r);
    return r;
}

long long __divdi3(long long a, long long b) {
    int neg = (a < 0) ^ (b < 0);
    unsigned long long q = __udivdi3(a < 0 ? -(unsigned long long)a : a,
                                     b < 0 ? -(unsigned long long)b : b);
    return neg ? -(long long)q : (long long)q;
}

long long __moddi3(long long a, long long b) {
    unsigned long long r;
    __udivmoddi4(a < 0 ? -(unsigned long long)a : a,
                 b < 0 ? -(unsigned long long)b : b, &r);
    return a < 0 ? -(long long)r : (long long)r;
}

/* ---- allocator: the console pool ----------------------------------------- */

/*
 * malloc/free on the kernel pool (xboxkrnl ExAllocatePool/ExFreePool, resolved
 * as imports by mktitle) -- the same allocator the retail CRT's heap sits on,
 * so there is no static reservation and pointers get the pool's alignment.
 */
extern void *ExAllocatePool(unsigned size);
extern void ExFreePool(void *base);

void *malloc(size_t n) {
    return ExAllocatePool(n ? (unsigned)n : 1u);
}

void free(void *p) {
    if (p)
        ExFreePool(p);
}

/* ---- MSVC compiler-version guards ---------------------------------------- */

/*
 * Every object the XDK's cl.exe (build 11886) emits references __C1_11886 and
 * __C2_11886 -- a link-time check that all objects came from the same compiler,
 * defined by the matching CRT. They carry no runtime meaning (no relocation uses
 * them), so a dummy definition is all a prebuilt object needs to link.
 */
int __C1_11886 = 0;
int __C2_11886 = 0;
