/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * 64-bit-safe accessors for the console timers, each writing its result through
 * a caller pointer.
 *
 * Why this is assembly. The Xbox 360 (Xenon) is a 64-bit PowerPC, and the MS ABI
 * returns a 64-bit integer in a SINGLE 64-bit register (r3) -- the kernel's
 * KeQuery* time functions hand their values back that way. Our clang targets
 * 32-bit `powerpc`, where an `unsigned long long` return is instead an r3:r4
 * register pair, so a C declaration returning `unsigned long long` reads r3
 * (high half) and r4 (uninitialised) and gets garbage. These thunks capture the
 * real single-register 64-bit result with a 64-bit store (`std`/`mftb`, which
 * our assembler emits on the 32-bit target even though the compiler will not)
 * and hand it back through memory, where the C caller reads it with ordinary
 * 32-bit loads -- no ABI ambiguity, correct for the full 64-bit width. A C
 * wrapper cannot do this; only assembly can. See docs/ilp32-ppc64-abi-plan.md.
 *
 *   void rxdk_query_perf_freq64(unsigned long long *out);      // time-base rate (Hz)
 *   void rxdk_query_interrupt_time64(unsigned long long *out); // 100ns since boot
 *   void rxdk_query_perf_counter64(unsigned long long *out);   // raw time-base ticks
 */

    .text

/* Shared prologue/epilogue for the "call a no-arg kernel export that returns a
   64-bit value in r3, store it through the pointer in r3-on-entry" pattern.
   `out` is held in r31 (callee-saved) so it survives the call. */
    .macro QUERY64 name, callee
    .globl \name
    .type  \name, @function
\name:
    mflr  0
    stw   0, -8(1)              /* save LR in the caller's frame (our convention) */
    stwu  1, -0x60(1)           /* allocate a frame; old SP -> backchain at 0(r1) */
    stw   31, 0x50(1)           /* preserve a nonvolatile to hold `out` across the call */
    mr    31, 3                 /* out -> r31 (callee-saved: survives the call) */
    bl    \callee               /* MS ABI: full 64-bit value returned in r3 */
    std   3, 0(31)              /* *out = the complete 64-bit register */
    lwz   31, 0x50(1)           /* restore the nonvolatile */
    addi  1, 1, 0x60            /* tear down the frame */
    lwz   0, -8(1)
    mtlr  0
    blr
    .size \name, . - \name
    .endm

    /* KeQueryPerformanceFrequency (xboxkrnl ord 0x83): the time-base rate. */
    QUERY64 rxdk_query_perf_freq64, KeQueryPerformanceFrequency

    /* KeQueryInterruptTime (xboxkrnl ord 0x82): 100ns intervals since boot. Its
       xenia shim returns qword_result_t whose result marshalling truncates to
       32 bits, so the value read back under xenia is only correct while it fits
       in 31 bits (~214s after boot); on real hardware r3 carries the full 64. */
    QUERY64 rxdk_query_interrupt_time64, KeQueryInterruptTime

/* The Xbox 360 has no KeQueryPerformanceCounter export -- the performance
   counter IS the PowerPC time-base register, which the XDK's own
   QueryPerformanceCounter reads with `mftb`. Read all 64 bits at once (the core
   is 64-bit, so a single mftb is atomic -- no high/low rollover race) and store
   them through the pointer. A leaf: no call, no frame. `out` is in r3; mftb into
   a scratch register so it is not clobbered. */
    .globl rxdk_query_perf_counter64
    .type  rxdk_query_perf_counter64, @function
rxdk_query_perf_counter64:
    mftb  11                    /* r11 = full 64-bit time base */
    std   11, 0(3)              /* *out = ticks */
    blr
    .size rxdk_query_perf_counter64, . - rxdk_query_perf_counter64
