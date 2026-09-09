# Modern C23 / C++23 runtime for Xbox 360

The toolchain so far reuses the shipped 2013 MS libraries: a title links the
translated `libcMT.a` for its C runtime (`memcpy`, `malloc`, the printf family,
the `__savegprlr_*` register helpers). That proves the translation works, but it
ties titles to a decade-old CRT. This is the plan to give the 360 target a
**modern, portable C23/C++23 runtime** instead, built from upstream sources the
way RXDK-Libs does it.

## Submodule layout (patches applied at build time)

Like RXDK-Libs, the upstream sources are **pristine git submodules**; our changes
live as tracked patches under `patches/` and are applied at build time by
`scripts/init-submodules.ps1` (idempotent -- a reverse-check skips a patch that is
already applied). Nothing local is committed into the submodule trees, so bumping
upstream is a re-pin plus a patch refresh.

| submodule | source | role |
|---|---|---|
| `vendor/picolibc` | picolibc/picolibc (pristine) | the C23 libc subset (string/mem/stdio/math), patched at build time |
| `vendor/llvm-project` | Team-Resurgent/llvm-project, branch `xbox360-msppc` | the patched MS-PPC clang **and** the C++ runtime sources (`libcxx`, `libcxxabi`, `libunwind`) |

The compiler patches are large and integral, so they live as commits on the fork's
`xbox360-msppc` branch rather than as build-time patches; the runtime-library
patches (picolibc, and any libcxx tweaks) are build-time patches under `patches/`.
`vendor/llvm-project` is a sparse checkout: the compiler cone (`clang`, `lld`,
`llvm`, `cmake`, `libc`, `third-party`) plus the runtime cone (`libcxx`,
`libcxxabi`, `libunwind`).

## Bring-up order

1. **picolibc (C23 libc).** Build it for `powerpc-unknown-xbox360` with the patched
   clang, providing the freestanding C library. Replace the pieces a title takes
   from `libcMT.a` (memcpy/malloc/string/stdio) with picolibc, keeping only the MS
   register helpers (`__savegprlr_*`, `_blkmov`) that are ABI glue.
2. **C++23 runtime.** `libcxxabi` (the Itanium-ish ABI: `__cxa_*`, guards,
   `operator new`/`delete`) + a freestanding `libcxx` subset (`<type_traits>`,
   `<utility>`, containers that do not need the OS) + `libunwind` for exceptions.
   Static-init (`.init_array`) is driven from the title's `_start`.
3. **Wire into mktitle.** A `--rt modern` mode links picolibc + the C++ runtime
   instead of the translated `libcMT.a`.

## Bulk regression harness

`tools/run_corpus.py` builds ALL the tests under `tests/corpus/` into **one**
sample and runs it **once** in xenia, then splits the `DbgPrint` output on
`== <name> ==` markers and diffs each section against its golden `expected.txt`.
Each test defines `void t_<name>(void)` (C++ tests mark it extern "C"); a
generated driver calls each behind its marker, linked with the shared
`_prelude/start.c` (which prints a completion sentinel and powers off). One build
and one xenia launch exercise every feature in a single address space, like a
real title -- the whole corpus runs in a few seconds.

```
python tools/run_corpus.py            # run + report
python tools/run_corpus.py --bless    # freeze current output as expected.txt
python tools/run_corpus.py --only hello,vcall
python tools/run_corpus.py --lib libcMT,xapilib
```

Robustness: the sentinel plus a retry absorb xenia's async-logger dropping the
last line on a fast exit; a `pending` file marks a probe that is known-blocked on
the current runtime (its build failure does not fail the run).

**Baseline (current translated CRT): 6 pass, 2 pending.** hello, arith, data
(writable globals), string (CRT strlen/memcpy/memcmp), malloc (CRT heap) and
vcall (C++ virtual dispatch, `-fno-exceptions -fno-rtti`) all pass. The two
`printf_*` probes are pending: MS's `sprintf` is not standalone -- it pulls in
the whole CRT stdio/heap/locale/atexit machinery (`__pioinfo`, `__onexitbegin`,
`GetProcessHeap`, `WriteFile`, ...) needing full CRT init, so it cannot link in
isolation. picolibc's `sprintf` is self-contained, so these resolve once the
modern runtime lands -- and they are exactly the tests that will catch printf
divergences (`%S` = wide string on MSVC, etc.) when it does.

Building the corpus surfaced a real translator gap: the MS CRT is full of COMDAT
sections (`.text$`/`.rdata$`), and `coff2elf` emitted their symbols as strong
globals, so pulling two CRT objects that both define, say, `_heap_init` or
`__locale_changed` was a duplicate-symbol link error. Fixed by emitting a
symbol defined in a COMDAT section as **weak** (MS's linker keeps one by name),
which is what let string/malloc/vcall link the CRT at all.

## picolibc bring-up: status

`tools/build_libc.py` compiles picolibc from `vendor/picolibc` with the patched
clang (`powerpc-unknown-xbox360`, MS ABI) straight to PPC ELF and archives it
into `build/libc/libc.a`. The config is `runtime/config/picolibc.h` (force-
included), and the flags mirror RXDK-Libs' `picolibcFlags` -- notably
`-fno-builtin` (picolibc's `memcpy`/`strlen` *are* the builtins, so recognising
their loops as the idiom is infinite self-recursion) and `-ffreestanding`.

- **string / ctype / errno / stdio build (372 objects).** picolibc is far more
  portable than the MS CRT -- these compiled for the PPC target essentially
  unchanged. tinystdio's `vfprintf.c` #includes its split parts
  (`vfprintf_float.c` etc.), so those are excluded from the glob; the Ryu float
  engines are excluded in favour of the classic dtoa/ftoa; the float requirement
  comes from `__IO_DEFAULT 'd'` in the config.
- **Self-contained `sprintf` with MSVC compatibility.** `runtime/xbox/ms_printf.c`
  (ported from RXDK-Libs) provides `sprintf`/`snprintf`/`swprintf`/`vsnprintf`
  over picolibc's `vfprintf`, rewriting the MSVC format first: `%S`/`%C` (the
  uppercase pair means the opposite width of the function), `%I64`/`%I32`, the
  `w` length modifier, and `%s`/`%c` following the function's own width in the
  wide family. `runtime/xbox/rt_support.c` supplies the 64-bit division helpers
  the compiler emits (`__udivdi3` etc., picolibc formats integers 64-bit) and a
  placeholder bump allocator for `malloc`/`free` (the real one will come from the
  console heap).
- **Titles compile C23 / C++23** now (`mktitle`, `-std=c23` / `-std=c++23`);
  picolibc itself stays at c17 (its own sources are c11/c17 -- the *runtime it
  provides* is what is C23/C++23-capable).

**The whole corpus runs on the modern runtime: 8/8, ~2 s.** `run_corpus.py`
defaults to linking `build/libc/libc.a` now, so every test -- string/mem, malloc,
and the two `printf_*` probes -- uses picolibc, not libcMT. The `%S` probe prints
`S=[WideStr]` / `ls=[WideLS]`: the MSVC wide-string divergence is handled. (Pass
`--lib libcMT` for the translated MS CRT, though its `sprintf` still cannot link
in isolation, so the printf tests need the modern runtime.)

Next: malloc from the console heap (replace the bump allocator); then the C++
runtime.

## Open issues found during bring-up

1. **FIXED -- MS-PPC return-address corruption (a compiler bug).** Non-leaf
   titles printed correct output then hung on return. Root cause was in the
   patched clang, not picolibc: the Xbox 360 ABI saves LR at `CallerSP-8`
   (`getReturnSaveOffset`), inside the negative callee-saved region, but
   `processFunctionBeforeFrameFinalized` packs the callee-saved GPRs from
   `CallerSP` downward with the second slot at `-8` -- so a saved GPR (r30)
   landed on top of the return address and `blr` jumped to garbage. SVR4 escapes
   this because its LR offset is positive (in the caller's frame). Fixed on the
   `xbox360-msppc` fork by reserving the LR doubleword before packing the CSR
   areas; `arith`/`string` now save r30 at `0x50` (LR at `0x58`) and power off
   cleanly. The corpus dropped from ~2 min of hang-retries to ~3 s, and the ABI
   still matches `cl.exe` 4/4. (`.rodata` is also on its own READONLY page now,
   so xenia's analyser no longer disassembles format strings as code -- that was
   only load-time noise, not the hang.)
2. **Prebuilt libs call the CRT.** The translated XDK libs (xapilib/d3d9/
   xgraphics) call `memcpy`/`malloc`/the printf trap path/`__savegprlr`
   internally; swapping libcMT -> picolibc must keep those working. Needs a
   corpus category that links a prebuilt lib against the modern runtime and
   checks a prebuilt-lib->CRT call path still behaves.

## Follow-ups noted

- **C++ exceptions/unwinding.** The C++23 runtime will need `libunwind` plus the
  same trick RXDK-Libs uses -- recover the `.eh_frame` length from the PE
  section table at runtime, because lld scatters archive `.eh_frame` -- and a
  catch-all/terminate path. Corpus probes (`throw`/`catch`) come with it.
- Console CRT (`_getch` and friends) is out of scope for a headless corpus (no
  input); those get linkage-only smoke tests.
