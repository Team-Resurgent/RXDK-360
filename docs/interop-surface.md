# Spike 2 — how much of the interop surface actually diverges?

Spike 1 showed the MS-PPC and LLVM PPC EABI conventions differ. This spike measures
*how much of the real API* is affected, in both directions.

## Direction A — MS libraries calling our CRT

The whole contract is 164 symbols (see README). Bucketed by ABI risk:

| bucket | count | note |
|---|---|---|
| `__savegprlr_*` / `__restgprlr_*` / `__save/restvmx_*` | 72 | hand-written asm anyway; no convention involved |
| pure pointer/integer, or pure floating-point signatures | 76 | **identical** under both conventions |
| divergent | 16 | `printf`/`sprintf`/`_snprintf`/`_vsnprintf`/`vsprintf_s`/`fprintf`/`vprintf`/`_snwprintf`/`swscanf_s` (varargs), `ldexp`/`modf` (`double,int`), `setjmp`/`longjmp`/`__jump_unwind` (buffer layout), `_cexit`/`_mtinit` |

**16 hand-written shims.** This direction is not a problem.

## Direction B — our code calling the MS libraries

Public C prototypes across all 190 headers in `include\xbox`, via `tools/header_scan.py`:

```
public C prototypes scanned: 4507
  DIVERGENT: 57 (1%)
    >8 params:        31
    int-after-float:  30
```

Named examples: `D3DDevice_Clear`, `Clear`, `ClearF`, `Resolve`, `EndTiling`,
`CreateVolumeTexture`, `CreateArrayTexture`.

Internal C++ exports, via `tools/abi_scan.py` (vftables, thunks and static data
excluded — they carry no calling convention):

| lib | mangled functions | ABI-identical | divergent | unparsed |
|---|---|---|---|---|
| `d3d9.lib` | 1209 | 802 | **30 (2%)** | 377 |
| `xgraphics.lib` | 6832 | 3433 | **58 (<1%)** | 3341 |
| `xapilib.lib` | 241 | 171 | **2 (<1%)** | 68 |

The unparsed remainder is dominated by template instantiations
(`??$D3DDWORDToPtr@...`) with trivially simple signatures.

So **argument placement diverges in roughly 1% of the API**. That part is a
generated-thunk problem, not a blocker.

## The catch: the parameter homing area is universal

Argument placement is not the whole convention. MS callees spill their incoming
register arguments into **the caller's frame**:

```c
int spill(int a, int b, int c, int d) { sink(&a); sink(&b); sink(&c); sink(&d); ... }
```

```
spill:
  mflr  r12
  stw   r12,-8(r1)
  stwu  r1,-60h(r1)        ; frame = 0x60, so new r1 = old r1 - 0x60
  stw   r3,74h(r1)         ; = old r1 + 0x14
  stw   r4,7Ch(r1)         ; = old r1 + 0x1C
  stw   r5,84h(r1)         ; = old r1 + 0x24
  stw   r6,8Ch(r1)         ; = old r1 + 0x2C
```

Those are exactly the homing slots deduced in spike 1 — base `r1+0x10`, 8-byte
stride, 32-bit value in the big-endian low word.

**Any caller must reserve `0x50` bytes (0x10 linkage + 8 slots x 8 bytes) above its
own stack pointer.** LLVM's PPC EABI reserves 8. An EABI-compiled caller that calls
*any* MS function which takes the address of a parameter — very common — has its own
locals silently overwritten.

This is not confined to the divergent 1%. It applies to every call into the MS libs.

## What this changes

Two ways to satisfy it:

**(a) Thunk every entry point.** Generated wrappers allocate the save area, shuffle
arguments, call, restore. No compiler change at all. Costs one extra call and a stack
adjust per API call. Fully mechanical — the headers and the mangled names carry
enough type information to generate it.

**(b) Teach LLVM to reserve the outgoing parameter area, then thunk only the ~57 + ~90
divergent functions.** This is a far smaller change than the "implement the whole MS
calling convention" that spike 1 implied — it is essentially the linkage-size and
parameter-area logic that `PPCFrameLowering` already carries for AIX, retargeted.

(b) is the better end state; (a) is a valid way to get running first and does not
foreclose (b).

## Still unmeasured

- Struct-by-value parameters in the public C API (the header scan does not detect
  them; the mangled-name scan found 11 in `d3d9`, 34 in `xgraphics`, 1 in `xapilib`).
- Callbacks — MS libraries calling *into* title code (comparators, thread entry
  points, D3D callbacks) need reverse thunks. Not yet enumerated.
- Whether MS varargs callees home all incoming GPRs. The probe was inconclusive
  because the test function never consumed its varargs.
