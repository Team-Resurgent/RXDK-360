/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * rxdk_query_perf_freq64 -- read the console time-base frequency as a full
 * 64-bit value, written through a caller pointer.
 *
 * Why this is assembly. The Xbox 360 (Xenon) is a 64-bit PowerPC, and the MS
 * ABI returns a 64-bit integer in a SINGLE 64-bit register (r3):
 * KeQueryPerformanceFrequency hands its rate back that way. Our clang targets
 * 32-bit `powerpc`, where an `unsigned long long` return is instead an r3:r4
 * register pair -- so a C declaration returning `unsigned long long` reads r3
 * (high half) and r4 (uninitialised) and gets garbage. Declaring the import as
 * 32-bit `unsigned` happens to work only because the rate fits in 32 bits;
 * that is fragile and wrong for any 64-bit-wide kernel return (a running
 * counter, an interrupt time). This thunk captures the real single-register
 * 64-bit result with a 64-bit store (`std`, which our assembler emits even on
 * the 32-bit target) and hands it back through memory, where the C caller reads
 * it with ordinary 32-bit loads -- no ABI ambiguity. See
 * docs/ilp32-ppc64-abi-plan.md for the underlying ABI gap.
 *
 *   void rxdk_query_perf_freq64(unsigned long long *out);   // *out = rate (Hz)
 */

    .text

    .globl rxdk_query_perf_freq64
    .type  rxdk_query_perf_freq64, @function
rxdk_query_perf_freq64:
    mflr  0
    stw   0, -8(1)              /* save LR in the caller's frame (our convention) */
    stwu  1, -0x60(1)           /* allocate a frame; old SP -> backchain at 0(r1) */
    stw   31, 0x50(1)           /* preserve a nonvolatile to hold `out` across the call */
    mr    31, 3                 /* out -> r31 (callee-saved: survives the call) */
    bl    KeQueryPerformanceFrequency   /* MS ABI: full 64-bit rate returned in r3 */
    std   3, 0(31)              /* *out = the complete 64-bit register */
    lwz   31, 0x50(1)           /* restore the nonvolatile */
    addi  1, 1, 0x60            /* tear down the frame */
    lwz   0, -8(1)
    mtlr  0
    blr
    .size rxdk_query_perf_freq64, . - rxdk_query_perf_freq64
