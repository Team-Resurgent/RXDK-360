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
  the compiler emits (`__udivdi3` etc., picolibc formats integers 64-bit) and
  `malloc`/`free` on the console pool -- `ExAllocatePool`/`ExFreePool`, the
  xboxkrnl exports the retail CRT's heap sits on, resolved as imports, so there
  is no static heap and titles stay small.
- **Titles compile C23 / C++23** now (`mktitle`, `-std=c23` / `-std=c++23`);
  picolibc itself stays at c17 (its own sources are c11/c17 -- the *runtime it
  provides* is what is C23/C++23-capable).

**The whole corpus runs on the modern runtime: 8/8, ~2 s.** `run_corpus.py`
defaults to linking `build/libc/libc.a` now, so every test -- string/mem, malloc,
and the two `printf_*` probes -- uses picolibc, not libcMT. The `%S` probe prints
`S=[WideStr]` / `ls=[WideLS]`: the MSVC wide-string divergence is handled. (Pass
`--lib libcMT` for the translated MS CRT, though its `sprintf` still cannot link
in isolation, so the printf tests need the modern runtime.)

## C++ runtime + pre-main init

`runtime/xbox/cxxrt.cpp` provides `operator new`/`delete` (over the console-pool
malloc) and the Itanium-ABI helpers the compiler references without exceptions:
`__cxa_pure_virtual`, `__cxa_atexit`/`__dso_handle` (a title powers off rather
than returning, so destructors do not run), and the single-threaded
`__cxa_guard_*` for function-static locals. `build_libc.py` compiles the `.cpp`
glue with the C++ flag set (c++23, freestanding, `-fno-exceptions -fno-rtti`).

**Pre-main init.** The linker script emits `.init_array` with
`__init_array_start`/`__init_array_end`, and `_prelude/start.c` runs it before
`title_main` -- the equivalent of the XDK's xapilib startup (XapiThreadStartup)
running the CRT init before `main`. The `cppinit` corpus test proves it: a global
object's constructor sets `g.v=1234` (0 if pre-main init never ran), `new`/`delete`
round-trip, and a function-static local constructs on first use. 9/9 corpus green.

Still to do: **exceptions** (libunwind + `__cxa_throw`/personality, plus the
RXDK-Libs trick of recovering the `.eh_frame` length from the PE section table at
runtime, since lld scatters archive `.eh_frame`), and -- because the unwinder
needs a real stack -- running `main` on its own thread sized from the XEX header
rather than on the kernel's small init thread (what xapilib's startup does).

## Validate against an official XEX

Build a title through the real XDK (`cl.exe` -> `link` -> `imagexex`) so it uses
the genuine xapilib startup, and compare: header layout, the pre-main init path,
and the DbgPrint output. That golden reference confirms our startup shim does the
equivalent of `XapiThreadStartup` and that the modern runtime matches the CRT
titles depend on.

## libc I/O + process hooks, threading, alignment (the extras)

Mirrored from RXDK-Libs (`runtime/xbox/`):

- **I/O + process hooks** (`libc_hooks.h`): `rxdk_set_stdin_handler` /
  `rxdk_set_output_handler` / `rxdk_set_exec_handler`. libc stays kernel-only, so
  it routes `read(0)`, `write(1/2)` and `execve` through these callbacks; with
  none set the defaults are EOF, `DbgPrint`, and `-1`. The `iohooks` corpus test
  installs an output hook (captures what `write(1)` emits) and a stdin hook
  (feeds `read(0)`) and confirms both fire -- the paths `printf`/`std::cout` and
  `getchar`/`std::cin` sit on. The uncaught-exception "hook" is ordinary
  `std::set_terminate` (standard libc++), so it arrives with the exception
  runtime.
- **Thread-safe** (`locks.c`): picolibc's retargetable locks are backed by kernel
  `RTL_CRITICAL_SECTION`s (recursive, lazily initialised), not no-ops -- a title
  that spawns threads gets real mutual exclusion around malloc/stdio/atexit.
- **16-byte-aligned malloc** (`rt_support.c`): the 360 CRT and `ExAllocatePool`
  only guarantee 8, but `__vector4`/XNAMath are `__declspec(align(16))` and `lvx`
  needs 16, so the allocator over-aligns to 16 (verified by the `malloc` test's
  `align16=1`); `calloc`/`realloc` track the request size in a small header.

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
3. **`printf` over the POSIX bufio crashes xenia (WIP).** `sprintf` (a string
   FILE) works, `write(1)` -> the output hook -> `DbgPrint` works, and the kernel
   critical sections work -- each verified in isolation -- but a `printf` through
   the buffered stdout FILE (`posix_stdio_streams.c` `FDEV_SETUP_POSIX`) faults in
   xenia's host with "invalid parameter to a service" before any output, i.e. in
   the bufio flush path. The `iohooks` test therefore drives the hooks through
   `write`/`read` directly; formatted output uses `sprintf` + `DbgPrint` for now.
   Next: debug the `__file_bufio` flush (likely a bad pointer/length reaching
   `write`).

2. **Prebuilt libs call the CRT -- register helpers supplied.** The translated
   XDK libs (xapilib/d3d9/xgraphics) reference, from the CRT, `memcpy`/`memmove`/
   `memset` (picolibc has these), the MS out-of-line register helpers
   `__savegprlr_14..29`/`__restgprlr_*`/`__savefpr_*`/`__restfpr_*` and `_blkmov`
   (picolibc does not -- our own clang inlines its saves), plus `__C1_11886`/
   `__C2_11886` (the MS CRT initializer-table sentinels) and `__C_specific_handler`
   (SEH). `build_libc.py` now pulls the verified `crtgpr.o`/`crtfpr.o` register
   helpers from the translated libcMT into `libc.a`, so MS-compiled code links
   against the modern runtime. The `apilib` corpus test calls xapilib's
   `OutputDebugStringA` linked against `libc.a` + `xapilib.a` and confirms it runs
   and returns. Still to wire: `_blkmov` (a `memmove` alias) and the CRT init
   table below.

## Other libraries hooking pre-main (the MS CRT init table)

Our own C++ static constructors run through the ELF `.init_array`. A prebuilt MS
library instead registers its pre-main initializers in `.CRT$XC*` sections (the
XDK CRT walks them between `__xc_a`/`__xc_z` via `_initterm`) -- a different
mechanism. Both are now bridged:

- `coff2elf` **keeps** the `.CRT$XC*` sections (it used to drop everything CRT),
  so a prebuilt lib's initializer pointers survive translation.
- the title's linker script gathers `*(SORT_BY_NAME(.CRT$XC*))` between
  `__xc_a`/`__xc_z`.
- `_prelude/start.c` walks that table (skipping the null XCA/XCZ markers, like
  `_initterm`) right after the `.init_array` walk, before `title_main`.

The `msinit` corpus test proves it: a constructor placed in `.CRT$XCU` (via a
section attribute, exactly how MS code registers one) sets `g_ms=4321` before
`title_main` runs -- through the MS path, not `.init_array`. So a prebuilt lib
can hook pre-main the way it does on a real title.

`__C1_11886`/`__C2_11886`, by contrast, turned out **not** to be init-table
bounds: no relocation uses them, and every object (even data-only ones) references
them -- they are MSVC's compiler-version consistency guards, defined by the
matching CRT. `rt_support.c` defines them as dummies so prebuilt objects link.

## Follow-ups noted

- **C++ exceptions/unwinding.** The C++23 runtime will need `libunwind` plus the
  same trick RXDK-Libs uses -- recover the `.eh_frame` length from the PE
  section table at runtime, because lld scatters archive `.eh_frame` -- and a
  catch-all/terminate path (`__C_specific_handler` for the MS libs). Because the
  unwinder needs a real stack, `main` should also run on its own thread sized
  from the XEX header rather than the kernel's small init thread -- what
  xapilib's startup does. Corpus probes (`throw`/`catch`) come with it.
- **Validate against an official XEX** built through `cl.exe`/`imagexex` (above).
- Console CRT (`_getch` and friends) is out of scope for a headless corpus (no
  input); those get linkage-only smoke tests.
