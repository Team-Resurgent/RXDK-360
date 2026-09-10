# MSVC C++ exception interop (the "#2 universal unwinder")

Goal: let the shipped cl.exe-built XDK libs that genuinely throw (xav, vcomp, and
future MS-C++ libs) run their C++ exceptions for real, instead of the loud-abort
stub `_CxxThrowException` from the #1 personality stub. This is the largest piece
of the CRT-parity work.

## Background

Two exception models coexist in an RXDK-360 title:

- **Ours** — Itanium C++ ABI over DWARF `.eh_frame`, driven by our libunwind
  (libcpp.a). Our own C++ code throws/catches this way. See
  `rxdk360-cpp-exceptions` memory / `docs/`.
- **MSVC** — the shipped libs use the Microsoft C++ EH model: per-function
  `.pdata` (RUNTIME_FUNCTION) → `.xdata` (UNWIND_INFO + a language handler
  `__CxxFrameHandler` + a `FuncInfo` describing try/catch/unwind states), driven
  by the kernel's SEH dispatcher. A `throw` is `_CxxThrowException(obj, throwinfo)`
  → `RaiseException(0xE06D7363, …)` → the kernel walks frames via the PE exception
  directory, calling each frame's language handler.

These are separate mechanisms; each works within its own frames. A throw that
must cross an MS↔ours frame boundary is out of scope (documented limit). In
practice the MS libs catch internally or expose C/HRESULT boundaries.

## Spike findings (2026-09-10)

- `tools/coff2elf.py` **keeps** `.pdata`/`.xdata` sections when translating MS
  objects (line ~235), so the frame info survives into the ELF we link.
- The packer (`tools/elf2xex.py` / `mktitle.py`) only sets up `.eh_frame` for our
  libunwind. It does **not** emit a PE exception directory
  (`IMAGE_DIRECTORY_ENTRY_EXCEPTION`) for `.pdata`. **This is the core blocker:**
  without it the kernel dispatcher can't find the MSVC frames.
- Kernel exports available (xboxkrnl.lib): `RaiseException`,
  `RtlLookupFunctionEntry`, `RtlVirtualUnwind`, `RtlUnwind`/`RtlUnwind2`,
  `RtlImageDirectoryEntryToData`, `KeGetCurrentProcessType`, `RtlCaptureContext`,
  `__C_specific_handler` (the C `__try/__except` handler — proves the kernel has
  an SEH dispatcher that drives language handlers).
- **Not** exported: `RtlInstallFunctionTableCallback`, `RtlAddFunctionTable`,
  `RtlDispatchException`. So function tables are **image-based** (found via the PE
  exception directory), not dynamically registered — the packer must lay them out.
- MS `__CxxFrameHandler` is reusable from libcMT. Its member's undefined externals:
  `__InternalCxxFrameHandler`, `__FrameUnwindToState`, `?__StateFromControlPc@@`,
  `?_inconsistency@@`, `__C_specific_handler` (kernel), `_getptd`, `memmove`,
  plus the save/restore GPR glue we already reuse. `_CxxThrowException` needs
  `RaiseException` (kernel) + `memcpy`.

## Plan (Path A — kernel dispatch + reuse the MS handler)

1. **Packer: emit the PE exception directory.** Collect the RUNTIME_FUNCTION
   entries from the linked `.pdata` (contributed by the MS objects), place them in
   a `.pdata` section in the XEX image, and set `IMAGE_DIRECTORY_ENTRY_EXCEPTION`
   (base + size) so the 360 loader/kernel registers the function table. Verify how
   retail XEXs expose `.pdata` (the XEX wrapper vs the inner PE image) — the kernel
   reading PE directories (`RtlImageDirectoryEntryToData` exists) suggests the
   inner PE image's directory is honored.
2. **Runtime: reuse the real MS handler.** Drop the #1 stubs
   (`__CxxFrameHandler`/`_CxxThrowException`/type_info vtable) and instead pull the
   real MS objects from libcMT via `build_libc.py`'s `extract_ms_glue`:
   `__CxxFrameHandler`, `__InternalCxxFrameHandler`, `__FrameUnwindToState`,
   `__StateFromControlPc`, `_inconsistency`, `_CxxThrowException`. Provide a
   compatible `_getptd` (per-thread EH state) over our TLS, or reuse MS's.
3. **RTTI.** Catch-type matching needs real MS RTTI (`type_info` + the
   `CatchableType`/`ThrowInfo` machinery, `__RTtypeid`/`__RTDynamicCast` if
   referenced) — the #1 stub type_info vtable is not enough for matching. Reuse the
   MS RTTI objects from libcMT.
4. **Test.** A minimal MSVC-ABI throw/catch (built with cl.exe, or a hand-crafted
   FuncInfo) run in xenia; then exercise an actual xav/vcomp throw path.

## Testability: xenia does not dispatch guest C++ EH (must be added)

Target is **real hardware**, where the console kernel dispatches. But our harness
is xenia, and xenia does **not** dispatch guest C++/SEH exceptions:

- `RtlRaiseException` (xboxkrnl_debug.cc) recognises the C++ code `0xE06D7363` and
  even parses the ThrowInfo/CatchableTypeArray (`x_s__ThrowInfo` etc. are already
  modelled), but `HandleCppException` ends at `XELOGE("Guest attempted to throw a
  C++ exception!")` -- "TODO: unwinding. This is going to suck." No unwind.
- `RtlUnwind` / `RtlVirtualUnwind` / `RtlLookupFunctionEntry` / `RtlCaptureContext`
  / `__C_specific_handler` are `kFunction` entries in `xboxkrnl_table.inc` with no
  `_entry` implementation -> auto-stubbed.
- xenia already reads `.pdata` on load, but only for JIT function discovery
  (`xex_module.cc` ~1565), not for exception dispatch.

So #2 is a **dual track**:

1. **RXDK side (HW-correct):** DONE -- packer emits the exception directory
   (`tools/elf2xex.py`). Remaining: drop the #1 stubs and reuse the real MS
   `__CxxFrameHandler`/`_CxxThrowException` + MS RTTI from libcMT via
   `extract_ms_glue`, provide `_getptd`.
2. **xenia side (testability, faithful to HW):** implement the C++ EH dispatch in
   `HandleCppException` so xenia behaves like the console kernel -- walk the guest
   PPC stack via the `.pdata`/`.xdata` we now emit, invoke the guest
   `__CxxFrameHandler` per frame (two-pass: find a catch matching a CatchableType,
   unwind running dtors), and transfer control to the catch (set guest PC/SP/regs
   and resume). The hard part is PPC virtual-unwind of guest frames + control
   transfer; the throw-struct parsing already exists. User has approved modifying
   xenia (built from source) for this.

Recommended order: RXDK runtime reuse (smaller, HW-correct, links the throwers)
first; then the xenia dispatcher (larger emulator feature) to make throws testable.

## Open risks

- Exact XEX ↔ PE-exception-directory mechanism on the 360 loader (step 1) — the
  main unknown to prototype first.
- `_getptd` layout expected by MS `__CxxFrameHandler`.
- Coexistence with our DWARF EH in one image (should be fine — disjoint frame
  descriptions, disjoint dispatch — but verify a title that uses both).

This does **not** close vcomp by itself: vcomp additionally needs the full MS STL
(basic_string/iostream/locale), a separate MS-STL object-reuse effort.
