<!--
SPDX-License-Identifier: GPL-3.0-or-later
2026 - Team Resurgent - Part of RXDK.
-->

# RXDK-360 SDK header layer

## Why

To build XDK-header code (D3D9, XGraphics, XAudio2, XNet, the platform/Win32
surface) with our modern toolchain we first tried coercing the *stock* MS XDK
headers with command-line flags:

    -fms-extensions -fms-compatibility -fdeclspec
    -D_WIN32 -D_M_PPCBE -D_M_PPC -D_XBOX -D__export= -D_SIZE_T_DEFINED

That got the whole tree to compile except `<xboxmath.h>` (VMX128 intrinsics) --
but it is the wrong foundation:

* It is fragile: every `#error Only Win32 target supported!` / `#error Must
  define a target architecture` gate, every ancient keyword (`__export`), every
  `__declspec` is being papered over from the outside.
* It collides with picolibc. The modern runtime's C library is picolibc, and the
  stock XDK CRT headers (`crtdefs.h`, `stdio.h`, `stdlib.h`, ...) redefine the
  very types picolibc owns (`size_t`, `wchar_t`, `__time64_t`), which is why
  `-D_SIZE_T_DEFINED` was needed. Two C libraries cannot both own the namespace.

The modernization to C23/C++23 means a title's include search path is a
*combination*: our modern runtime headers + picolibc (the C library) + the XDK
API headers. Those have to form one coherent set, not the stock MS headers held
together with flags.

## Approach: generate a clean header set from the user's stock XDK

Mirror what coff2elf does for libraries: a reproducible tool transforms the
user's *stock* `include\xbox` into a curated, clang+picolibc-compatible copy.
Users supply their own XDK, so the transformation must be a script (no
hand-patched, redistributed MS headers) -- the same constraint that governs the
`.lib` -> `.a` path.

    tools/gen_sdk_headers.py  <stock include\xbox>  ->  build/sdk/include

Transformations, applied uniformly:

1. **Delegate the C library to picolibc.** Drop / neutralise the XDK CRT headers
   (`crtdefs.h`, `vadefs.h`, `stdio.h`, `stdlib.h`, `string.h`, `math.h`, ...);
   the curated platform headers pull the C types from picolibc instead, so
   there is exactly one owner of `size_t`/`wchar_t`/`FILE`/etc. Keep only the
   XDK-specific declarations.
2. **Remove the target gates.** Replace the `#if !defined(_WIN32) #error` and
   `#error Must define a target architecture` blocks with the known target --
   the header set is for exactly one platform, so the gates are dead weight.
3. **One owned compat prelude, not scattered edits.** A single
   `rxdk_platform.h` (force-included) provides what the MS compiler predefined
   and clang does not: `_WIN32`, `_M_PPCBE`, `_M_PPC`, `_XBOX`, the neutralised
   `__export`, `__declspec` shims, MS integer keywords. This is the one place
   the "guard changes" live, and it is ours.
4. **`__declspec`/`__fastcall`/`__export` etc.** handled by `-fms-extensions`
   plus the prelude, or rewritten where an attribute has no clang equivalent.
5. **`xboxmath.h` / xnamath** is the one real gap: it needs the MS VMX128
   intrinsics (`__vector4`, `__lvx`, `__fctidz`, ...). Provide a clean
   replacement built on clang's PPC/AltiVec vector types (or, near-term, have
   titles use the D3DX matrix math in d3dx9, which already works), rather than
   the MS intrinsic header.

## Include-path model

A title compiles with, in order:

    -I build/sdk/include            # curated XDK API (D3D9/XGraphics/XAudio2/XNet/...)
    -I <picolibc>/include           # the C library (size_t, stdio, math, ...)
    -I runtime/xbox + our libc++    # the modern C++23 runtime
    -include rxdk_platform.h        # the one owned compat prelude

ABI is unchanged: the curated headers keep every struct layout, enum value and
function signature byte-identical to the XDK (they must, to call the shipped
`.lib` code) -- only the CRT-ownership and target-gate scaffolding changes.

## Build order

1. `rxdk_platform.h` prelude + the Win32/platform slice (`winnt.h`/`windef.h`
   surface) as the foundation.
2. The D3D9 + XGraphics + D3DX9 slice -- enough to build the spinning-triangle
   title (the first milestone), using D3DX matrix math to sidestep xboxmath.
3. Broaden to XAudio2/X3DAudio/XNet and the rest as the component dashboard grows.
4. A clean `xboxmath.h` replacement (VMX128 -> clang vectors) when the math API
   is actually needed by title code.

## Relationship to the rest

* Libraries: coff2elf (`.lib` -> `.a`). Imports: gen_import_stubs. Headers:
  gen_sdk_headers (this). All three regenerate from the user's stock XDK.
* This is the header half of "make our libc/libcpp a drop-in for the official
  CRT" -- see the libc-surface-parity work.
