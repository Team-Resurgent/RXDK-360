# RXDK-360

An attempt at a cross-platform, modern-language toolchain for the Xbox 360 that reuses
the shipped XDK libraries rather than reimplementing them.

## The idea

RXDK works for the original Xbox because Xbox 1 is x86 COFF/PE — a format LLVM and lld
already speak. The 360 is PPC COFF/XEX, which LLVM has never supported, so the same
playbook does not transfer.

The hybrid approach: translate the XDK's `lib\xbox\*.lib` from PPC COFF to PPC32 ELF
**once, offline**, then build everything with the patched **clang** (a
`powerpc-unknown-xbox360` target that reproduces the platform's MS-PPC ABI) + `lld`
on any host and pack the result into a XEX. That keeps the 2.7 GB of working Microsoft
libraries and still gives a cross-platform build with a C23/C++23-capable compiler.
(An earlier route used `zig cc` with the PPC EABI, but that ABI does not match the
console compiler's — see below — so the patched clang is the real toolchain now.)

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

## Testing

Code from the patched toolchain executes correctly on a real PowerPC engine.
`tools/gentests.py` compiles a C test with our clang, links it, and produces the
`.bin`/`.map` pair xenia's PowerPC test runner needs -- replacing the custom
binutils that `xb gentests` would otherwise require:

```
$ python tools/gentests.py tests/instr_rxdk_abi.c -o build/tests --run
instr_rxdk_abi: 192 bytes at 0x80000000, 3 tests
Total tests: 3   Passed: 3   Failed: 0
```

Registers are zeroed, `REGISTER_IN` values placed, the function runs to its
`blr`, and `REGISTER_OUT` values checked -- so these confirm the argument
convention end to end, not just that the right instructions were emitted.
`tests/instr_rxdk_abi.c` includes the case that separates this ABI from the ELF
one: with `(int, float, int, double, int)` the compiler emits

```
add 3, 5, 3     ; c arrives in r5, not r4
add 3, 3, 7     ; e arrives in r7, not r5
```

Running a whole title under the emulator is not working yet. Xenia detects
module format by magic bytes and has an `ElfModule`, and our linked ELF matches
everything it validates (`ET_EXEC`, `EM_PPC`, `PT_LOAD` segments inside
`0x80000000-0x9FFFFFFF`), but it crashes during load with nothing useful in the
buffered log. The PowerPC test runner is the better harness regardless, so that
is where correctness work should go until there is a reason to need the full
emulator.

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

Since then the pipeline has come up end to end: there is an ELF-to-XEX packer
(`elf2xex` / XexTool `pack`), titles load and run in the **xenia** emulator calling
the kernel, and there is a full modern **replacement CRT** — a picolibc-based
`libc.a` plus a `libcpp.a` (libunwind + libc++abi) covering C23/C++23 with working
exceptions, threads, signals and timers, validated by a stdlib suite (53 sections /
734 checks, all green). Not yet on real hardware. Remaining ABI gaps: variadic
functions are still routed through thunks (the platform's varargs layout differs
structurally from the 32-bit ELF one) and some by-value struct passing still follows
ELF rather than the platform's rules.

## The toolchain

`vendor/llvm-project` is a git submodule (see `.gitmodules`) tracking branch
`xbox360-msppc`, which adds a `powerpc-unknown-xbox360` target implementing the
platform ABI. Initialise it (and the other submodules) after cloning with
`git submodule update --init --recursive`. See
[docs/llvm-patch-design.md](docs/llvm-patch-design.md).

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
vendor/        llvm-project, picolibc and xextool (git submodules)
```

## Prerequisites

- Xbox 360 XDK, full install (not `InstallType=Minimum` — that omits `include\`,
  `lib\` and the compiler). Windows only, and needed only for the spikes that compare
  against `cl.exe` and for the shipped `lib\xbox\*.lib` the toolchain reuses.
- The patched clang + lld, built once from the `vendor/llvm-project` submodule via
  `tools/build-llvm.bat`. This is the real toolchain.
- zig 0.16.0 or newer — optional, only for the legacy PPC-EABI path (`mktitle.py
  --cc zig`) and the early ABI spikes.
