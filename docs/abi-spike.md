# Spike 1 — MS-PPC (Xenon) vs LLVM PPC EABI calling convention

**Question.** The hybrid plan is: translate the shipped Xbox 360 XDK libraries from PPC COFF
to PPC32 ELF once, offline, then build everything cross-platform with `zig cc` + `lld`.
That only works if zig-generated code can *call* MS-generated code. Does the calling
convention match?

**Answer. No.** Five independent divergences, none of them edge cases. The object-format
half of the hybrid is easy; the ABI half needs an LLVM backend change, not a shim.

## Method

`spike/abi/abi.c` defines call sites with signatures that probe each ABI rule. It is
compiled twice from identical source — MS `cl.exe 16.00.11886.00 for PowerPC` and
`zig cc -target powerpc-freestanding-eabihf` (zig 0.16.0) — and the argument placement
compared. See `spike/abi/build.sh`.

## Findings

### 1. Stack parameter slots: 8 bytes (MS) vs 4 bytes (EABI)

`many(1..12)` — args 9-12:

| | arg9 | arg10 | arg11 | arg12 |
|---|---|---|---|---|
| MS  | `stw r,0x54(r1)` | `0x5C` | `0x64` | `0x6C` |
| zig | `stw r,8(r1)`    | `12`   | `16`   | `20`   |

MS uses an 8-byte slot per argument with the 32-bit value in the big-endian *low* word
(slot+4). Slots therefore start at `r1+0x50` for arg9, i.e. the parameter save area
base is `r1+0x10` with `0x40` of homing space reserved for the eight register args.
zig packs 4-byte slots starting at `r1+8` with no homing area.

Confirmed from the callee side too — `def_many` reads its stack args at
`0x54/0x5C/0x64/0x6C` off the *incoming* `r1`, with no prologue stack adjustment.

### 2. Mixed int/float lists assign different GPRs

`mixed(int a, float b, int c, double d, int e, float f)`:

| | a | b | c | d | e | f |
|---|---|---|---|---|---|---|
| MS  | r3 | fr1 | **r5** | fr2 | **r7** | fr3 |
| zig | r3 | f1  | **r4** | f2  | **r5** | f3  |

MS consumes one *positional slot* per argument and skips the GPR when the slot goes to
an FPR (PowerOpen/AIX rule). EABI advances GPR and FPR counters independently. This is
the worst of the five: it breaks ordinary 6-argument calls, and interleaved int/float
signatures are everywhere in a graphics and audio API.

### 3. Small structs by value: register (MS) vs pointer (zig)

`sbv(struct {int a,b;} s, int x)`:

```
MS   stw r11,0x50(r1); stw r10,0x54(r1); ld r3,0x50(r1)   ; whole 8-byte struct in one GPR
zig  stw r3,8(1); stw r3,12(1); addi 3,1,8                ; pointer to a temporary in r3
```

### 4. Varargs pass floating-point in GPRs (MS)

`va("%d %d %f", 1, 2, 3.0)`:

```
MS   r3=fmt  r4=1  r5=2  stfd fr1,0x28(r1); ld r6,0x28(r1)   ; the double's raw bits in a GPR
```

EABI varargs use the `va_list` gpr/fpr-counter scheme with FP in FPRs.

### 5. Link register save slot

```
MS   mflr r12; stw r12,-8(r1); stwu r1,-N(r1)      ; LR at (incoming r1) - 8
zig  mflr 0;   stwu 1,-N(1);   stw 0,N+4(1)        ; LR at (incoming r1) + 4
```

MS also uses the area below the incoming SP as a save region (`std r31,-8(r1)` in leaf
functions), and calls out-of-line `__savegprlr_N` / `__restgprlr_N` helpers — which is
why those 72 helpers appear in the CRT dependency set.

## What this means

The MS Xenon convention is recognisable: it is essentially **64-bit PowerPC
(PowerOpen/AIX) parameter passing with a 32-bit data model** — 8-byte parameter slots,
a homing area, positional GPR/FPR slot consumption, small structs in registers, varargs
FP in GPRs — combined with ILP32 pointers and MS-specific frame slots.

That matters because LLVM already implements almost exactly this parameter lowering for
`powerpc64-ibm-aix`. The work is therefore **"AIX64 argument lowering + ILP32 pointers +
MS LR/frame slots" as a new calling convention in `PPCISelLowering`**, not a convention
designed from scratch. Still a real LLVM patch, but a much smaller one than it first
appears, and it can be validated function-by-function against `cl.exe` output using this
same spike harness.

## Status

- [x] ABI divergence characterised
- [ ] LLVM `CallingConv::MSPPC` prototype
- [~] COFF -> ELF archive translator (5 reloc types: ADDR32, REL24, REFHI, REFLO, PAIR)
      parser + input survey done, see [coff-translation.md](coff-translation.md); ELF emitter next
- [ ] VMX128 register set + encodings in the PPC backend
- [ ] ELF -> XEX packer
