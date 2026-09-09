#ifndef RXDK_PPC_ASM_H
#define RXDK_PPC_ASM_H
/*
 * Minimal replacement for binutils' ppc-asm.h -- just the two macros picolibc's
 * powerpc setjmp.S includes it for (FUNC_START/FUNC_END). We do not ship the
 * full binutils header; these expand to the standard ELF function-symbol
 * directives ('.' is the current location, ';' the GAS statement separator).
 */
#define FUNC_NAME(name)  name
#define FUNC_START(name) .globl name; .type name, @function; name:
#define FUNC_END(name)   .size name, . - name
#endif
