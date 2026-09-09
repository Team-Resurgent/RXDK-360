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

`tools/run_corpus.py` (planned): a corpus of small C/C++ programs under
`tests/corpus/`, each with an expected-output file. For each program it builds
through `mktitle.py`, runs it headless in xenia, captures the `DbgPrint` output and
diffs it against the expected. Establish a **green baseline against the current
translated CRT first**, then bring up the modern runtime piece by piece and keep the
harness green -- that is what catches regressions as the runtime is swapped in.

Each corpus program `_start`s, exercises one feature (string ops, malloc/free,
a container, a virtual call, a thrown exception), prints a deterministic line
through `DbgPrint`, and returns via `HalReturnToFirmware`. The expected file is the
line(s) it should print.
