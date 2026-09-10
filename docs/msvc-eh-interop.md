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

### Track 1 precise scope (spike result)

Reusing the real MS `__CxxFrameHandler` is viable via a SURGICAL extraction, but
only with `_getptd`/`terminate` as a shim boundary -- the naive closure drags the
entire MS CRT (21 members: crt0*/thread/tidtable/mlock/dosmap/winxfltr…, 12
symbol collisions incl. exit/_errno/_mtinit, 14 Win32 APIs) because the EH code's
`_getptd` pulls MS's whole per-thread-data + threading + init subsystem.

With `_getptd` + `?terminate@@YAXXZ` treated as shims-we-provide, it collapses to
**9 EH-core members** (extract via build_libc `extract_ms_glue`, translated names):
`frame.o handlers.o throw.o unwind.o ehstate.o trnsctrl.o exsup.o hooks.o
validate.o`. Then: NO real collisions (only my #1 stubs `__CxxFrameHandler`/
`_CxxThrowException`, which get dropped); the only externals are `_getptd`,
`?terminate@@YAXXZ` (we provide -> abort / our terminate), and `RaiseException`
(kernel). Catch-matching uses inline CatchableType pointer comparison (no
`__RTtypeid` pulled), so the #1 stub type_info vtable stays fine.

**Crux / open work:** `_getptd` must return an MS `_tiddata`/`_ptd`-LAYOUT-
compatible per-thread struct (the EH objects read specific offsets:
_curexception/_curcontext/_ProcessingThrow/_pFrameInfoChain/_translator/…). The
XDK ships no mtdll.h, so the layout must be reverse-engineered from the objects
(disasm the `bl _getptd` + subsequent `lwz off(r3)` loads) or from the public VC
CRT mtdll.h, then verified.

### Coupling + recommendation

Track 1 adds NO new linkability (throwers already link via the #1 stubs -> still
127/129); its only payoff is HW-correct throw behaviour, which is UNVERIFIABLE
without track 2 (RaiseException is a xenia stub) or real HW. So track 1 and track
2 are COUPLED: a testable throw/catch needs the real handler (1) AND xenia
dispatch (2) together. Swapping the verified, working #1 stubs for an unverifiable
`_getptd`-guessed engine risks regressing the non-throwers (d3dx9 etc. work
today). Recommendation: KEEP the #1 stubs, and do track 1 + track 2 as ONE
coupled HW-EH subproject with the xenia dispatcher as the test bed -- not a
delicate, untestable reuse landed on its own.

## Track 2 (xenia dispatcher) -- test bed, building blocks, algorithm

**Test bed (done):** `tests/eh/build.py` -- a minimal cl.exe `/EHsc` throw/catch
linked with the real `libcMT` -> a stock XEX with MS `__CxxFrameHandler` +
`.pdata`/`.xdata`. Decoupled from the RXDK runtime side, so track 2 is validated
on its own. **Baseline in stock xenia:** prints `before-throw` and `after-catch`
plus "Guest attempted to throw a C++ exception!", but NOT `caught-int` -- the
catch is skipped (`RtlRaiseException` logs and returns without dispatching).
**Target: `[T] caught-int 1234`.**

**xenia building blocks (confirmed):**
- call a guest function from a shim: `processor()->Execute(thread_state, addr,
  args[], arg_count)` (used for APCs / thread-notify / Ob callbacks).
- throwing thread + registers: `XThread::GetCurrentThread()->thread_state()->
  context()` (ppc_context).
- guest memory: `kernel_memory()->TranslateVirtual`.
- `HandleCppException` already parses the EXCEPTION_RECORD -> ThrowInfo ->
  CatchableTypeArray.

**Dispatcher algorithm (in HandleCppException / RtlRaiseException), matching the
real 360 kernel SEH dispatcher:**
1. Seed CONTEXT = the throwing thread's ppc_context (PC/SP/regs at the `throw`).
2. Loop over frames:
   a. `RtlLookupFunctionEntry(PC)` -> RUNTIME_FUNCTION from the loaded module's
      `.pdata` (the PE exception directory we now emit). 
   b. read its `.xdata` UNWIND_INFO -> language handler (__CxxFrameHandler addr)
      + handler data (FuncInfo); build a DISPATCHER_CONTEXT in guest memory.
   c. call the guest `__CxxFrameHandler` via `processor()->Execute(...)` with
      (record, establisher_frame, context, dispatcher). It does the C++ match;
      on a catch it calls `RtlUnwind` (transfers control, does not return here).
   d. else `RtlVirtualUnwind` to the caller frame; continue.

**Hard parts (the "this is going to suck"):**
- **RtlVirtualUnwind for PPC/360** -- interpret the 360 `.xdata` UNWIND_INFO
  encoding to restore non-volatile regs + compute caller SP/PC. The 360 PPC
  unwind-data format must be reverse-engineered/understood first.
- **RtlUnwind control transfer** -- the handler calls it to run cleanup + resume
  at the catch funclet; xenia must set the guest context (PC=catch, SP=target)
  and continue guest execution there rather than returning to the shim.

These two (plus `RtlLookupFunctionEntry`) are unimplemented table stubs in xenia
today; implementing them is the bulk of track 2. Track 1's real MS handler then
rides the same dispatcher on the RXDK-runtime side.

## Xenon MSVC-EH data format (REVERSE-ENGINEERED from tests/eh/tc.obj)

The critical unknown for `RtlLookupFunctionEntry`/`RtlVirtualUnwind` -- now cracked:

- **RUNTIME_FUNCTION** (`.pdata`, 8 bytes, big-endian): `BeginAddress` (RVA) then a
  packed word: `PrologLength:8 | FunctionLength:22 | ThirtyTwoBit:1 |
  ExceptionFlag:1` (lengths in INSTRUCTIONS; ×4 = bytes). Decode from the word W:
  `PrologLen = W & 0xFF; FuncLen = (W>>8)&0x3FFFFF; 32bit = (W>>30)&1; Exc =
  (W>>31)&1`. Verified: rxdk_eh_test W=0xC0001C07 -> prolog 7, func 28, Exc.
- **Handler pair**: when Exc=1, the two DWORDs at **`BeginAddress - 8`** are
  `[__CxxFrameHandler RVA][FuncInfo (_s_FuncInfo) RVA]`. (In tc.obj the function
  symbol is at .text+8, and .text@0x0/@0x4 relocate to __CxxFrameHandler /
  __ehfuncinfo$rxdk_eh_test.)
- **FuncInfo** (`_s_FuncInfo`, in .rdata): magic/maxState/pUnwindMap/nTryBlocks/
  pTryBlockMap/... -> `__unwindtable$` / `__tryblocktable$` / `__catchsym$`. Catch
  types reference CatchableTypeArray -> CatchableType -> type_info (`??_R0H@8` for
  int) with {properties, pType, displacement(0,-1,0), size, copyFn}.
- **Funclets**: each catch handler is a SEPARATE function with its own `.pdata`
  entry AND its own `[handler,FuncInfo]-8` pair (tc.obj: main + 2 funclets ->
  3 `.pdata` entries).
- **Prolog** is regular MS-PPC (`.text+8: mflr r12 (7d8802a6); stw r12,-8(r1)
  (9181fff8); stwu r1,-N(r1); bl __savegprlr_N/__savefpr_N/__savevmx_N; ...`), so
  `RtlVirtualUnwind` replays `PrologLen` instructions to restore SP/LR/nonvols.

### xenia implementation TODO (against this format)
1. `RtlLookupFunctionEntry(PC)`: binary-search the loaded module's `.pdata`
   (exception directory) for `BeginAddress <= PC < BeginAddress + FuncLen*4`.
2. dispatch (in HandleCppException): from the throw context, loop -- look up the
   RF, read `[handler,FuncInfo]` at `BeginAddress-8`, `processor()->Execute` the
   guest `__CxxFrameHandler(record, frame, ctx, dispatcher)`; on
   ExceptionContinueSearch, `RtlVirtualUnwind` to the caller and continue.
3. `RtlVirtualUnwind`: decode the prolog (PrologLen insns from BeginAddress) --
   handle mflr/stw-LR/stwu-frame + the __save{gpr,fpr,vmx} helper calls -- to
   restore the caller's SP/LR/nonvolatiles into the CONTEXT.
4. `RtlUnwind`: second pass -- run cleanup funclets, then set the guest context
   (PC=catch funclet, SP=target frame) and resume there instead of returning.

## xenia dispatcher draft (WIP)

First-draft implementation lives in the xenia tree (built from source, not this
repo): `src/xenia/kernel/xboxkrnl/xboxkrnl_eh.cc`. NOT yet added to the xenia
build (so it can't break it). Implemented correct-by-construction against the
decoded format:
- `DecodeRuntimeFunction` (the PrologLen/FuncLen/32Bit/Exc word),
- `LookupFunctionEntry` (RtlLookupFunctionEntry: scan the module's .pdata for the
  RF covering a PC),
- `ReadHandlerPair` (the `[handler,FuncInfo]` at BeginAddress-8),
- `DispatchCppException` skeleton (walk frames, per-frame call the guest
  `__CxxFrameHandler`).

Remaining (the build-loop coding, marked TODO in the file):
1. wire the .pdata base/count out of XexModule (downcast cpu::Module* ->
   XexModule, GetPESection(".pdata") / exception directory),
2. marshal a guest DISPATCHER_CONTEXT + CONTEXT and `processor()->Execute` the
   guest `__CxxFrameHandler(record, frame, ctx, dispatcher)`,
3. `RtlVirtualUnwind`: decode PrologLen instructions (mflr/stw-LR/stwu +
   __save{gprlr,fpr,vmx}_N) to recover caller SP/LR (needed for OUTER-frame
   catches; same-frame catches resolve without it),
4. `RtlUnwind` control transfer: set guest PC=catch funclet / SP=target frame and
   resume there,
5. register RtlLookupFunctionEntry/RtlVirtualUnwind/RtlUnwind in
   xboxkrnl_table.inc + call DispatchCppException from HandleCppException.

The same-frame catch (test bed) should resolve first with 1+2+4; multi-frame
needs 3.

### INCREMENT 1 VERIFIED (built + ran in xenia)

The dispatcher was built into xenia (Ninja Multi-Config + MSVC, `xb build
--config release --target xenia-app`; the RECURSIVE glob picks up the new file on
reconfigure) and run against the test bed. With `--log_level=3` (Debug) it logs:

    EH: throw pc=82016358 func@820162E8 prolog=3 len=32 exc=0

So `LookupFunctionEntry` (the `.pdata` scan) + the RUNTIME_FUNCTION word decode
WORK on a live throw. IMPORTANT correction it revealed: `pc = ctx->lr` at the
RtlRaiseException shim points into **`_CxxThrowException`** (`exc=0`, no EH), not
the throwing function -- the throw is NESTED 2 frames below the catch
(RaiseException -> _CxxThrowException -> rxdk_eh_test). So `RtlVirtualUnwind` is
required EVEN for a "same-frame" catch: unwind up from `_CxxThrowException`
(decode its prolog to recover the caller SP + saved LR = return into
rxdk_eh_test) until a frame with `exc=1` whose `__CxxFrameHandler` matches. The
"same-frame resolves without RtlVirtualUnwind" note above was wrong.

Build loop confirmed working (~2 min rebuild + run).

### INCREMENT 2 VERIFIED (frame walk reaches the catching handler)

The dispatcher now walks guest frames from the throw to the catching frame, live:

    EH: frame 0 pc=82016358 func@820162E8 exc=0     (_CxxThrowException)
    EH: frame 1 pc=820137CC func@82013760 exc=0     (throw helper)
    EH: frame 2 pc=82010048 func@82010008 exc=1     (rxdk_eh_test)
    EH: -> catching frame __CxxFrameHandler@820132D8 FuncInfo@82000584

⭐ The MS-PPC unwind is simply the **back-chain** (`caller_sp = *sp`) with the
**LR save slot at `CallerSP-8`** (`ret_pc = *(caller_sp - 8)`) -- CONFIRMED
correct (reached the exc=1 frame in 2 hops; `[csp+4]`/`[sp+4]` were 0). So a full
prolog-instruction decoder is NOT needed for standard framed functions -- the
back-chain + fixed LR slot suffices. So `RtlLookupFunctionEntry` +
`RtlVirtualUnwind` + catching-frame handler resolution all WORK.

### Remaining (the final stretch)

1. Marshal a guest `CONTEXT` (the establisher frame's register state) + a
   `DISPATCHER_CONTEXT` (`{ControlPc, ImageBase, FunctionEntry, EstablisherFrame,
   ContextRecord, LanguageHandler=__CxxFrameHandler, HandlerData=FuncInfo,
   TargetIp}`) in guest scratch, then `processor()->Execute(thread_state,
   handler, {record, establisher_frame, context, dispatcher}, 4)` -- the MSVC
   __CxxFrameHandler does the type match against the FuncInfo try/catch tables.
2. `RtlUnwind`: on a catch, the handler calls it -- run cleanup funclets, then set
   the guest context (PC = catch funclet, SP = establisher) and resume there.
   Needs the DISPATCHER_CONTEXT/CONTEXT MS layouts (RE next) + control transfer.

Draft (builds + runs, increments 1-2 verified) in
`xenia:src/xenia/kernel/xboxkrnl/xboxkrnl_eh.cc`.

### INCREMENT 3-4 VERIFIED -- CATCH RUNS CORRECTLY (test bed PASSES)

The dispatcher now catches for real. Full live output on the test bed:

    [T] before-throw
    [T] caught-int 1234     <-- catch funclet ran with the correct value
    [T] after-catch
    [T] ALLDONE

End-to-end host-side dispatch working in xenia (`xboxkrnl_eh.cc`):
1. RtlLookupFunctionEntry -- scan the exe .pdata for the throw pc.
2. Frame walk -- back-chain (`caller_sp=*sp`) + LR@`CallerSP-8` up to the exc=1
   frame.
3. Parse FuncInfo/TryBlockMap/HandlerType; match the thrown CatchableType(s)
   against each catch's type_info (address-equal within a module; pType==0 =
   catch(...)).
4. Copy the thrown object to `sp + dispCatchObj`.
5. ⭐ Run the catch funclet with the establisher frame pointer **in r12** (the
   MSVC PPC funclet does `addi r31,r12,-0x70; lwz r4,0x50(r31)` == reads the
   catch object at `[r12 + dispCatchObj]`). `ctx->r[12] = sp` + the object at
   `sp+dispCatchObj` makes it self-consistent. `processor()->Execute` runs it;
   it returns the continuation IP.

### Remaining refinements (generality; test bed already passes)

- **Honor the continuation IP:** currently the funclet runs via Execute and the
  stub-return falls through into _CxxThrowException, which returns to just past
  the try -- which happens to equal the continuation here, so the observable
  output is correct. For general programs, explicitly resume the guest at the
  funclet's returned IP with SP=establisher (needs xenia's shim-return redirect).
- **Second-pass cleanup/unwind:** run destructor funclets (UnwindMap) between the
  throw state and the catch; multiple/nested try blocks; state from the
  ip2state map rather than assuming the single try.
- **Outer-frame catches:** the register-accurate CONTEXT for handlers that read
  more than the catch object (back-chain gives SP/PC; add nonvol restore).
- Register RtlLookupFunctionEntry/RtlVirtualUnwind/RtlUnwind in xboxkrnl_table.inc
  if guest code calls them directly.

### Continuation + destructor findings (2nd test bed: tests/eh/tc2 with a dtor)

- **Continuation resume works for the common case, by construction:** xenia uses
  an LR-sentinel (0xBCBCBCBC) return model and recompiles guest `bl` as host
  calls, so the intermediate frames (_CxxThrowException, throw helper) unwind
  **naturally via normal host returns** back to the catching frame at the
  continuation. That is why the fall-through lands correctly -- not luck.
- **Destructors run but in the WRONG order.** With a destructible local in the try
  (`Guard g`), the output is `caught-int 1234` THEN `dtor-7` THEN `after-catch`.
  Correct C++ is `dtor-7` (2nd-pass unwind) BEFORE `caught-int`. My dispatch runs
  the catch funclet first (via Execute); the destructor runs afterward via the
  natural unwind. Running the 2nd-pass destructors explicitly BEFORE the catch AND
  keeping the natural fall-through would double-destroy.

**Conclusion / architectural frontier:** getting destructor ordering + a fully
general continuation right requires **explicit control transfer** -- perform the
whole EH sequence (2nd-pass UnwindMap destructors in order -> catch -> resume at
the funclet's continuation IP with SP=establisher) and DO NOT let the intermediate
recompiled frames return naturally. In xenia's model (guest frames are host
frames) that means host-level stack unwinding / longjmp from the shim -- the deep
"this is going to suck" piece. The current dispatcher is correct for
catch-value + no-destructor and runs destructors (mis-ordered) otherwise; full
ordering correctness is the remaining hard work.

## Open risks

- Exact XEX ↔ PE-exception-directory mechanism on the 360 loader (step 1) — the
  main unknown to prototype first.
- `_getptd` layout expected by MS `__CxxFrameHandler`.
- Coexistence with our DWARF EH in one image (should be fine — disjoint frame
  descriptions, disjoint dispatch — but verify a title that uses both).

This does **not** close vcomp by itself: vcomp additionally needs the full MS STL
(basic_string/iostream/locale), a separate MS-STL object-reuse effort.
