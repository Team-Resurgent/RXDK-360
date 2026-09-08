# The complete MS-PPC (Xenon) vs LLVM PPC EABI delta

Every property below was measured by compiling identical source with MS
`cl.exe 16.00.11886.00 for PowerPC` and `zig cc -target powerpc-freestanding-eabihf`
(zig 0.16.0) and comparing generated code. Probes live in `spike/abi`, `spike/abi2`,
`spike/vmx`.

## A. Universal properties — affect *every* call, cannot be shimmed cheaply

### A1. Parameter homing area (mandatory 0x50 reservation)

MS callees write incoming register arguments into **the caller's frame**. Two
independent confirmations:

```
spill(int a,int b,int c,int d) with &a taken:      ; frame 0x60
  stw r3,74h(r1)  stw r4,7Ch(r1)                  ; = old r1 + 0x14, +0x1C
  stw r5,84h(r1)  stw r6,8Ch(r1)                  ; = old r1 + 0x24, +0x2C

vsum(int n, ...) consuming its varargs:
  std r4,18h(r1) ... std r10,48h(r1)              ; homes r4-r10, 8-byte stride
```

Save area base `r1+0x10`, 8 bytes per slot, 32-bit values in the big-endian low word.
**Every caller must reserve 0x50 bytes.** LLVM's PPC EABI reserves 8. A caller that
does not reserve it has its own locals silently overwritten by any MS function that
takes the address of a parameter or consumes varargs.

### A2. Vector registers v20-v31 are volatile under MS

MS's callee-saved vector registers are `vr108`-`vr127`, via out-of-line helpers:

```
undefined externals of a vector-heavy MS object:  __savevmx_108  __restvmx_108
referenced across the shipped libs:  __save/restvmx_111,112,115,118,120,122,123,124
```

Everything MS preserves lives at vr108+, inside the VMX128-only range. So MS treats
`vr0`-`vr107` — including SysV's callee-saved `v20`-`v31` — as **volatile**. LLVM code
holding a vector in `v20` across a call into an MS lib loses it. Shimming this would
cost 12 vector save/restores per call; it is a register-class change instead.

### A3. GPRs are 64-bit; callee-saved GPRs must be saved 64-bit wide

MS saves callee-saved GPRs with `std`/`ld` (`std r31,-8(r1)`), and holds 64-bit values
in single registers. LLVM's 32-bit PPC target saves with `stw`/`lwz`. An MS -> our-code
callback that preserves only the low 32 bits of r14-r31 corrupts any 64-bit value the
MS caller was holding.

### A4. Link register save slot

MS `mflr r12; stw r12,-8(r1); stwu r1,-N(r1)` -> LR at (incoming r1) - 8.
zig `mflr 0; stwu 1,-N(1); stw 0,N+4(1)` -> LR at (incoming r1) + 4.
Matters for unwinding and debuggers rather than for correctness of calls.

## B. Bounded properties — specific signatures, thunkable

| property | MS | LLVM PPC EABI |
|---|---|---|
| stack parameter slots | 8 bytes, area base `r1+0x10` | 4 bytes, base `r1+8` |
| mixed int/float lists | one positional slot per arg; GPR skipped for an FP arg | independent GPR and FPR counters |
| vector arguments | `vr1`-`vr13` | `v2`-`v13` |
| vector args consuming a GPR slot | no (float args do) | n/a |
| `long long` argument | **one** GPR (64-bit registers) | `r3:r4` register pair |
| struct by value, <= 8 bytes | in one GPR as a doubleword | pointer to a temporary |
| struct by value, 16 / 24 bytes | consecutive GPRs r3,r4(,r5) | pointer |
| struct return, <= 8 bytes | in r3 | pointer |
| struct return, > 8 bytes | hidden sret pointer in r3 | **same** |

## C. Properties that agree

- Callee-saved GPRs `r14`-`r31`, FPRs `f14`-`f31` (both).
- Stack alignment 16 bytes (MS frames observed: 0x60, 0x90, 0xF0, 0x100).
- First 8 integer/pointer args in `r3`-`r10`, first FP args in `f1`-`f13`, integer
  return in `r3`, FP return in `f1`.
- Large struct return via hidden pointer in `r3`.

## D. How much of the API is affected by the bounded set

Public C prototypes across all headers (`tools/surface_scan.py`, with typedefs
resolved so enums and scalar typedefs are not miscounted as aggregates):

```
public C prototypes : 4820, divergent 94 (2%)
    >8 params:         37
    64-bit int param:  31
    int-after-float:   30
    struct-by-value:    2
callback typedefs   : 83, divergent 5 (6%)
    >8 params: 3   64-bit int param: 2   int-after-float: 1
```

Internal C++ exports (`tools/abi_scan.py`, vftables/thunks/static data excluded):
`d3d9` 30/1209, `xgraphics` 58/6832, `xapilib` 2/241 — all under 2%.

CRT symbols the MS libs require of us: 164 total — 72 asm register helpers, 76
identical under both conventions, **16 divergent** (printf family, `ldexp`, `modf`,
`setjmp`/`longjmp`, `_cexit`, `_mtinit`).

## E. XNAMath is not a link dependency

```
XM* among UNDEFINED externals (we must supply):  0
XM* among DEFINED exports:  d3d9.lib 170,  xgraphics.lib 203
```

The libraries carry their own already-compiled copies, VMX128 and all, and never call
back to us for math. We ship whatever math library we like. The ~3270 VMX128 intrinsic
uses in `xnamath*.inl` are header inline code we are free not to compile.

Caveat: because the libs *define* COMDAT `XM*` symbols, our math library must not reuse
those names, or the linker could select our EABI-vector-ABI copy for an MS caller
passing in `vr1`. Namespacing avoids it.

## F. Consequence for the plan

Three universal properties (A1, A2, A3) are register-class and frame-layout facts. They
argue for the LLVM change (option b) rather than pure thunking — thunking A2 alone
would cost 12 vector spills per call.

Everything in B is bounded at roughly **99 signatures** (94 public prototypes + 5
callbacks) plus ~90 internal C++ exports and the 16 CRT shims. That is a generated
thunk layer, not hand work.

## G. Still unverified

- Exact red-zone depth below `r1` that MS assumes for leaf functions (observed uses to
  -0x40; not pinned to a documented limit).
- Whether the XEX loader or kernel imposes further constraints on frame layout.
- Behaviour of `setjmp`/`longjmp` buffer layout (counted as divergent, not decoded).
