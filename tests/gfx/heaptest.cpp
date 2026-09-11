/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 *
 * Minimal process-heap stress repro. XUI faults inside xapilib's process-heap
 * allocator (SplitFreeBlock) under heavy use, while the MS-linked build of the
 * same code does not -- suggesting our pipeline mistranslates xapilib's heap.
 * This isolates the heap from XUI: hammer HeapAlloc/HeapFree on the process
 * heap (the same path XuiAlloc uses) with mixed sizes and a churn pattern, so
 * a SplitFreeBlock fault reproduces without any of XUI in the way.
 */
#include <xtl.h>

extern "C" int DbgPrint(const char *, ...);

int main(void)
{
    DbgPrint("[HEAP] start; ProcessHeap=%p\n", GetProcessHeap());

    /* Phase 1: many small allocations kept live (forces the free list to grow
       and commit new pages -- this is where XUI's fault appears). */
    enum { N = 4096 };
    static void *p[N];
    for (int i = 0; i < N; ++i) {
        DWORD sz = 16 + (i * 24) % 512;       /* 16..528 bytes, varied */
        p[i] = HeapAlloc(GetProcessHeap(), 0, sz);
        if (!p[i]) { DbgPrint("[HEAP] alloc %d failed\n", i); break; }
        for (DWORD b = 0; b < sz; ++b) ((BYTE *)p[i])[b] = (BYTE)i;   /* touch */
        if ((i & 511) == 0) DbgPrint("[HEAP] phase1 %d ok (last=%p)\n", i, p[i]);
    }
    DbgPrint("[HEAP] phase1 done\n");

    /* Phase 2: free every other block, then reallocate into the holes -- churn
       that exercises SplitFreeBlock coalescing/splitting. */
    for (int i = 0; i < N; i += 2) if (p[i]) { HeapFree(GetProcessHeap(), 0, p[i]); p[i] = 0; }
    DbgPrint("[HEAP] phase2 freed evens\n");
    for (int i = 0; i < N; i += 2) {
        DWORD sz = 32 + (i * 40) % 1024;
        p[i] = HeapAlloc(GetProcessHeap(), 0, sz);
        if (!p[i]) { DbgPrint("[HEAP] refill %d failed\n", i); break; }
    }
    DbgPrint("[HEAP] phase2 refilled\n");

    /* Phase 3: HEAP_ZERO_MEMORY allocations (XuiAlloc zero-fills) + HeapReAlloc
       growth, the two patterns the plain phases above did not cover. */
    for (int i = 0; i < N; ++i) if (p[i]) { HeapFree(GetProcessHeap(), 0, p[i]); p[i] = 0; }
    for (int i = 0; i < N; ++i) {
        DWORD sz = 20 + (i * 13) % 300;
        p[i] = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sz);      /* 0x8 */
        if (!p[i]) { DbgPrint("[HEAP] zalloc %d failed\n", i); break; }
    }
    DbgPrint("[HEAP] phase3 zeroed allocs done\n");
    for (int i = 0; i < N; ++i) {
        if (!p[i]) continue;
        DWORD sz = 40 + (i * 57) % 900;
        void *q = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, p[i], sz);
        if (q) p[i] = q;
    }
    DbgPrint("[HEAP] phase3 realloc done\n");
    for (int i = 0; i < N; ++i) if (p[i]) HeapFree(GetProcessHeap(), 0, p[i]);
    DbgPrint("[HEAP] DONE\n");
    return 0;
}
