/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * setjmp/longjmp for the Xbox 360 (PowerPC, MS-PPC ILP32). picolibc ships a
 * powerpc setjmp.S but it needs binutils' ppc-asm.h and uses a `bclr+` branch
 * hint clang's integrated assembler rejects, so we provide our own -- it saves
 * the same state the ABI makes a function preserve: the callee-saved GPRs
 * (r14-r31, plus r1/r2/r13), LR, CR, and the callee-saved FPRs (f14-f31).
 *
 * jmp_buf is double[32] (picolibc machine/setjmp.h, __PPC__ non-ALTIVEC ->
 * _JBLEN 32, _JBTYPE double = 256 bytes, 8-aligned). Layout, in bytes from r3:
 *   0   r1 (SP)      4  r2       8  r13
 *   12..80   r14..r31 (18 words)
 *   84  LR           88 CR
 *   96..232  f14..f31 (18 doubles, 8-aligned)
 */

    .text

    .globl setjmp
    .type  setjmp, @function
setjmp:
    stw   1, 0(3)
    stw   2, 4(3)
    stw   13, 8(3)
    stw   14, 12(3)
    stw   15, 16(3)
    stw   16, 20(3)
    stw   17, 24(3)
    stw   18, 28(3)
    stw   19, 32(3)
    stw   20, 36(3)
    stw   21, 40(3)
    stw   22, 44(3)
    stw   23, 48(3)
    stw   24, 52(3)
    stw   25, 56(3)
    stw   26, 60(3)
    stw   27, 64(3)
    stw   28, 68(3)
    stw   29, 72(3)
    stw   30, 76(3)
    stw   31, 80(3)
    mflr  0
    stw   0, 84(3)
    mfcr  0
    stw   0, 88(3)
    stfd  14, 96(3)
    stfd  15, 104(3)
    stfd  16, 112(3)
    stfd  17, 120(3)
    stfd  18, 128(3)
    stfd  19, 136(3)
    stfd  20, 144(3)
    stfd  21, 152(3)
    stfd  22, 160(3)
    stfd  23, 168(3)
    stfd  24, 176(3)
    stfd  25, 184(3)
    stfd  26, 192(3)
    stfd  27, 200(3)
    stfd  28, 208(3)
    stfd  29, 216(3)
    stfd  30, 224(3)
    stfd  31, 232(3)
    li    3, 0
    blr
    .size setjmp, . - setjmp

    .globl longjmp
    .type  longjmp, @function
longjmp:
    lwz   1, 0(3)
    lwz   2, 4(3)
    lwz   13, 8(3)
    lwz   14, 12(3)
    lwz   15, 16(3)
    lwz   16, 20(3)
    lwz   17, 24(3)
    lwz   18, 28(3)
    lwz   19, 32(3)
    lwz   20, 36(3)
    lwz   21, 40(3)
    lwz   22, 44(3)
    lwz   23, 48(3)
    lwz   24, 52(3)
    lwz   25, 56(3)
    lwz   26, 60(3)
    lwz   27, 64(3)
    lwz   28, 68(3)
    lwz   29, 72(3)
    lwz   30, 76(3)
    lwz   31, 80(3)
    lwz   0, 84(3)
    mtlr  0
    lwz   0, 88(3)
    mtcrf 255, 0
    lfd   14, 96(3)
    lfd   15, 104(3)
    lfd   16, 112(3)
    lfd   17, 120(3)
    lfd   18, 128(3)
    lfd   19, 136(3)
    lfd   20, 144(3)
    lfd   21, 152(3)
    lfd   22, 160(3)
    lfd   23, 168(3)
    lfd   24, 176(3)
    lfd   25, 184(3)
    lfd   26, 192(3)
    lfd   27, 200(3)
    lfd   28, 208(3)
    lfd   29, 216(3)
    lfd   30, 224(3)
    lfd   31, 232(3)
    cmpwi 4, 0
    bne   1f
    li    4, 1
1:
    mr    3, 4
    blr
    .size longjmp, . - longjmp
