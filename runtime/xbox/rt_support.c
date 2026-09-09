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

/* ---- placeholder allocator ----------------------------------------------- */

static char g_heap[512 * 1024];
static size_t g_used;

void *malloc(size_t n) {
    n = (n + 15) & ~(size_t)15;
    if (g_used + n > sizeof(g_heap))
        return NULL;
    void *p = g_heap + g_used;
    g_used += n;
    return p;
}

void free(void *p) {
    (void)p;                       /* bump allocator: no reclaim yet */
}
