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
#include <stdint.h>

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

/* ---- allocator: 16-byte-aligned over the console pool -------------------- */

/*
 * malloc/free on the kernel pool (xboxkrnl ExAllocatePool/ExFreePool, resolved
 * as imports) -- the allocator the retail CRT's heap sits on, so there is no
 * static reservation.
 *
 * Alignment is 16. The 360 SDK's own malloc.h notes the MS CRT defaults to only
 * 8-byte alignment (16-aligned vectors are meant to use _aligned_malloc), and
 * ExAllocatePool likewise returns 8-aligned user pointers (its 8-byte
 * X_POOL_ALLOC_HEADER over a coarser block). But __vector4 / XNAMath's XMVECTOR
 * are __declspec(align(16)) and the VMX128 load (lvx) needs 16, so a plain `new`
 * of a vector-bearing type would otherwise be under-aligned. We therefore
 * over-allocate and bump the user pointer to a 16-byte boundary, with a header
 * just below it recording the real pool pointer (for free) and the request size
 * (for realloc) -- the same reasoning behind RXDK-Libs' 16-aligning heap.
 */
extern void *ExAllocatePool(unsigned size);
extern void ExFreePool(void *base);
extern void *memcpy(void *d, const void *s, size_t n);

#define RXDK_MALLOC_ALIGN 16u

typedef struct {
    void  *raw;                        /* the ExAllocatePool pointer to free */
    size_t size;                       /* the requested size, for realloc */
} rxdk_alloc_hdr;

void *malloc(size_t n) {
    size_t total = n + sizeof(rxdk_alloc_hdr) + (RXDK_MALLOC_ALIGN - 1);
    void *raw = ExAllocatePool((unsigned)total);
    if (!raw)
        return 0;
    uintptr_t base = (uintptr_t)raw + sizeof(rxdk_alloc_hdr);
    uintptr_t user = (base + (RXDK_MALLOC_ALIGN - 1)) & ~(uintptr_t)(RXDK_MALLOC_ALIGN - 1);
    rxdk_alloc_hdr *h = (rxdk_alloc_hdr *)(user - sizeof(rxdk_alloc_hdr));
    h->raw = raw;
    h->size = n;
    return (void *)user;
}

void free(void *p) {
    if (!p)
        return;
    rxdk_alloc_hdr *h = (rxdk_alloc_hdr *)((uintptr_t)p - sizeof(rxdk_alloc_hdr));
    ExFreePool(h->raw);
}

void *calloc(size_t nmemb, size_t size) {
    if (size && nmemb > (size_t)-1 / size)   /* overflow */
        return 0;
    size_t n = nmemb * size;
    void *p = malloc(n);
    if (p) {
        char *c = (char *)p;
        for (size_t i = 0; i < n; i++)
            c[i] = 0;
    }
    return p;
}

void *realloc(void *p, size_t n) {
    if (!p)
        return malloc(n);
    if (n == 0) {
        free(p);
        return 0;
    }
    rxdk_alloc_hdr *h = (rxdk_alloc_hdr *)((uintptr_t)p - sizeof(rxdk_alloc_hdr));
    size_t old = h->size;
    void *np = malloc(n);
    if (np) {
        memcpy(np, p, old < n ? old : n);
        free(p);
    }
    return np;
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

/* ---- process/abort + aligned_alloc + assert ------------------------------ */

extern void HalReturnToFirmware(unsigned);

/* No process to signal on the console: a fatal fault powers the box off, the
   same end state a real title reaches. */
void abort(void) {
    HalReturnToFirmware(0);
    for (;;) {}
}

/* C11 aligned_alloc. malloc already returns 16-byte-aligned blocks (see above),
   which covers every alignment the C++ exception runtime asks for (the unwind
   exception header is 8/16-aligned). Larger alignments are not needed yet. */
void *aligned_alloc(size_t alignment, size_t size) {
    (void)alignment;
    return malloc(size);
}

/* picolibc's assert() (no-message form) lands here on failure. */
void __assert_no_args(void) {
    abort();
}
