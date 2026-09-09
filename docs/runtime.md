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

`tools/run_corpus.py` builds a corpus of small C/C++ programs under
`tests/corpus/`, runs each headless in xenia and diffs the `DbgPrint` output
against a golden `expected.txt`. Each program implements `title_main()`; the
shared `_prelude/start.c` runs it, prints a completion sentinel, and powers off.

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

## Follow-ups noted

- **C++ exceptions/unwinding.** The C++23 runtime will need `libunwind` plus the
  same trick RXDK-Libs uses -- recover the `.eh_frame` length from the PE
  section table at runtime, because lld scatters archive `.eh_frame` -- and a
  catch-all/terminate path. Corpus probes (`throw`/`catch`) come with it.
- Console CRT (`_getch` and friends) is out of scope for a headless corpus (no
  input); those get linkage-only smoke tests.
