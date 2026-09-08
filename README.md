# RXDK-360

An attempt at a cross-platform, modern-language toolchain for the Xbox 360 that reuses
the shipped XDK libraries rather than reimplementing them.

## The idea

RXDK works for the original Xbox because Xbox 1 is x86 COFF/PE — a format LLVM and lld
already speak. The 360 is PPC COFF/XEX, which LLVM has never supported, so the same
playbook does not transfer.

The hybrid approach: translate the XDK's `lib\xbox\*.lib` from PPC COFF to PPC32 ELF
**once, offline**, then build everything with `zig cc` + `lld` on any host and pack the
result into a XEX. That keeps the 2.7 GB of working Microsoft libraries and still gives
a cross-platform build with a C23/C++23-capable compiler.

## What we know so far

Measured against XDK 2.0.21256.17 (`cl.exe 16.00.11886.00 for PowerPC`).

**Encouraging:**

- Argument placement actually diverges in only ~**1%** of the API: 57 of 4507 public C
  prototypes, and <2% of the internal C++ exports. The CRT side needs just **16** shims.

- The CRT contract is tiny. Across `d3d9`, `xgraphics`, `xapilib`, `xuirun`, `st`,
  `xact3`, `xnet`, `xonline` there are only **164** CRT symbols referenced — 72 of them
  PPC register save/restore helpers, 4 operator new/delete, ~84 plain C functions.
  All 164 come from `libcMT.lib`; `libcpMT.lib` contributes nothing unique.
- **No C++ exception or RTTI dependency** — no `__CxxFrameHandler`, no
  `_CxxThrowException`, no `type_info`. The shipped libs are built EH-off, RTTI-off.
- Only **5 real relocation types** in the objects: `ADDR32`, `REL24`, `REFHI`, `REFLO`,
  `PAIR` (plus `SECTION`/`SECREL`, which are debug-info only and droppable). All map
  1:1 onto PPC32 ELF relocations.
- `zig cc -target powerpc-freestanding-eabihf` produces correct ELF32 big-endian PPC
  with working AltiVec codegen, today, on any host.

**Blocking:**

- The MS calling convention is **not** LLVM's PPC EABI. Five independent divergences,
  measured in [docs/abi-spike.md](docs/abi-spike.md).
- MS callees spill incoming register arguments into **the caller's** frame at
  `r1+0x10..0x4F`. Every caller must reserve `0x50` bytes; LLVM's PPC EABI reserves 8.
  This applies to *every* call into the MS libs, not just the divergent ones. See
  [docs/interop-surface.md](docs/interop-surface.md).
- **VMX128** — Xenon's 128-vector-register extension, used throughout `xboxmath.h`,
  `vectorintrinsics.h`, `d3d9types.h`, `XDSP.h` — does not exist in LLVM. Only
  32-register AltiVec does.
- LLVM has no `xenon` or `cell` CPU model for PowerPC.

## Status of the open questions

Every ABI property has now been measured rather than assumed - see
[docs/abi-complete.md](docs/abi-complete.md) for the full delta.

| question | answer |
|---|---|
| VMX128 fatal? | **No** - not required by the API, and XNAMath is not a link dependency |
| struct-by-value in the public C API | **2** prototypes |
| callbacks needing reverse thunks | **5** of 83 callback typedefs |
| varargs callees home incoming GPRs? | **Yes** - confirmed, makes the 0x50 reservation mandatory |
| MS callee-saved vector set | **vr108-vr127** - so v20-v31 are volatile to MS (universal hazard) |
| callee-saved GPR/FPR sets | r14-r31 / f14-f31 - **agree** with SysV |
| stack alignment | 16 bytes - **agrees** |
| total divergent public signatures | **94 of 4820 (2%)** |

Three properties are universal and want the LLVM change (parameter homing area,
volatile v20-v31, 64-bit-wide callee-saved GPRs). The rest is a generated thunk layer
over ~99 signatures plus 16 CRT shims.

## Where it stands

The patched clang builds and reproduces the platform compiler's ABI on every
rule the harness checks:

```
$ RXDK_CLANG=... python tools/verify_abi.py
stack parameter slots      OK    cl=[84, 92, 100, 108]  clang=[84, 92, 100, 108]
mixed int/float registers  OK    cl=[f1, f2, f3, r3, r5, r7]  clang=[f1, f2, f3, r3, r5, r7]
vector argument registers  OK    cl=[1, 2]  clang=[1, 2]
return address slot        OK    cl=[-8]  clang=[-8]

4 agree, 0 differ, 0 skipped
```

The base target is `powerpc-unknown-xbox360`. An earlier attempt to build on the
64-bit base failed for reasons worth reading: see
[docs/base-target-correction.md](docs/base-target-correction.md).

Not yet done: variadic functions are deliberately left to thunks, since the
platform's varargs layout differs structurally from the 32-bit ELF one; struct
passing still follows the ELF rules rather than the platform's; and nothing has
run on hardware or in an emulator. There is no ELF-to-XEX packer yet, and no
replacement CRT.

## The toolchain

`vendor/llvm-project` (gitignored; clone it yourself) carries branch
`xbox360-msppc`, which adds a `powerpc64-unknown-xbox360` target implementing the
platform ABI. See [docs/llvm-patch-design.md](docs/llvm-patch-design.md).

```
tools/build-llvm.bat    build the patched clang + lld (PowerPC backend only)
tools/verify_abi.py     compile the probes with both cl.exe and the patched
                        clang and report agreement, rule by rule
```

`verify_abi.py` is the acceptance test: every row corresponds to one measured
property in [docs/abi-complete.md](docs/abi-complete.md).

## Layout

```
docs/          findings and design notes
spike/abi/     stack layout, mixed int/float, struct and varargs probes
spike/abi2/    callee-side probes: structs, 64-bit args, varargs homing, red zone
spike/vmx/     vector ABI and callee-saved vector register probes
tools/         scanners, toolchain build, ABI verification
vendor/        llvm-project checkout (not tracked)
```

## Prerequisites

- Xbox 360 XDK, full install (not `InstallType=Minimum` — that omits `include\`,
  `lib\` and the compiler). Windows only, and needed only for the spikes that compare
  against `cl.exe`.
- zig 0.16.0 or newer.
